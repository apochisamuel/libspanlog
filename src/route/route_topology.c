#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Network topology graph for route computation.
 * Models the cluster as a weighted directed graph where nodes
 * are storage servers and edges are network links with latency weights.
 *
 * Uses Dijkstra's algorithm for shortest path computation.
 */

#define SPL_TOPO_MAX_NODES 64
#define SPL_TOPO_MAX_EDGES 256
#define SPL_TOPO_INF 0xFFFFFFFF

typedef struct {
    uint32_t from;
    uint32_t to;
    uint32_t weight;  /* latency in microseconds */
    bool     active;
} spl_topo_edge_t;

typedef struct {
    uint32_t node_ids[SPL_TOPO_MAX_NODES];
    uint32_t node_count;
    spl_topo_edge_t edges[SPL_TOPO_MAX_EDGES];
    uint32_t edge_count;
    /* Adjacency: adj[i] = index of first edge from node i */
} spl_topology_t;

int spl_topo_init(spl_topology_t *topo) {
    if (!topo) return -1;
    memset(topo, 0, sizeof(*topo));
    return 0;
}

int spl_topo_add_node(spl_topology_t *topo, uint32_t node_id) {
    if (!topo || topo->node_count >= SPL_TOPO_MAX_NODES) return -1;
    for (uint32_t i = 0; i < topo->node_count; i++) {
        if (topo->node_ids[i] == node_id) return -1;
    }
    topo->node_ids[topo->node_count++] = node_id;
    return 0;
}

int spl_topo_add_edge(spl_topology_t *topo, uint32_t from, uint32_t to,
                      uint32_t weight) {
    if (!topo || topo->edge_count >= SPL_TOPO_MAX_EDGES) return -1;

    spl_topo_edge_t *e = &topo->edges[topo->edge_count++];
    e->from = from;
    e->to = to;
    e->weight = weight;
    e->active = true;
    return 0;
}

static int find_node_idx(const spl_topology_t *topo, uint32_t node_id) {
    for (uint32_t i = 0; i < topo->node_count; i++) {
        if (topo->node_ids[i] == node_id) return (int)i;
    }
    return -1;
}

/*
 * Dijkstra's shortest path from source to all reachable nodes.
 * dist[i] = shortest distance from source to node_ids[i].
 * prev[i] = predecessor node index on shortest path.
 */
int spl_topo_shortest_path(const spl_topology_t *topo, uint32_t source,
                           uint32_t *dist, int32_t *prev) {
    if (!topo || !dist || !prev) return -1;

    int src_idx = find_node_idx(topo, source);
    if (src_idx < 0) return -1;

    bool visited[SPL_TOPO_MAX_NODES] = {false};
    uint32_t n = topo->node_count;

    for (uint32_t i = 0; i < n; i++) {
        dist[i] = SPL_TOPO_INF;
        prev[i] = -1;
    }
    dist[src_idx] = 0;

    for (uint32_t step = 0; step < n; step++) {
        /* Find unvisited node with minimum distance */
        int u = -1;
        uint32_t min_dist = SPL_TOPO_INF;
        for (uint32_t i = 0; i < n; i++) {
            if (!visited[i] && dist[i] < min_dist) {
                min_dist = dist[i];
                u = (int)i;
            }
        }
        if (u < 0) break;
        visited[u] = true;

        /* Relax edges from u */
        uint32_t u_id = topo->node_ids[u];
        for (uint32_t e = 0; e < topo->edge_count; e++) {
            if (!topo->edges[e].active) continue;
            if (topo->edges[e].from != u_id) continue;

            int v = find_node_idx(topo, topo->edges[e].to);
            if (v < 0 || visited[v]) continue;

            uint32_t alt = dist[u] + topo->edges[e].weight;
            if (alt < dist[v]) {
                dist[v] = alt;
                prev[v] = u;
            }
        }
    }

    return 0;
}

/*
 * Find the k nearest nodes to source.
 * Returns them sorted by distance.
 */
int spl_topo_k_nearest(const spl_topology_t *topo, uint32_t source,
                       uint32_t k, uint32_t *out_nodes, uint32_t *out_dists) {
    if (!topo || !out_nodes) return -1;

    uint32_t dist[SPL_TOPO_MAX_NODES];
    int32_t prev[SPL_TOPO_MAX_NODES];
    spl_topo_shortest_path(topo, source, dist, prev);

    /* Sort by distance (selection sort) */
    uint32_t indices[SPL_TOPO_MAX_NODES];
    for (uint32_t i = 0; i < topo->node_count; i++) indices[i] = i;

    for (uint32_t i = 0; i < topo->node_count && i < k; i++) {
        uint32_t min_j = i;
        for (uint32_t j = i + 1; j < topo->node_count; j++) {
            if (dist[indices[j]] < dist[indices[min_j]])
                min_j = j;
        }
        if (min_j != i) {
            uint32_t tmp = indices[i];
            indices[i] = indices[min_j];
            indices[min_j] = tmp;
        }
    }

    uint32_t collected = 0;
    for (uint32_t i = 0; i < topo->node_count && collected < k; i++) {
        uint32_t idx = indices[i];
        if (topo->node_ids[idx] == source) continue;
        if (dist[idx] == SPL_TOPO_INF) continue;
        out_nodes[collected] = topo->node_ids[idx];
        if (out_dists) out_dists[collected] = dist[idx];
        collected++;
    }

    return (int)collected;
}

/*
 * Compute cluster diameter: longest shortest path between any pair.
 */
uint32_t spl_topo_diameter(const spl_topology_t *topo) {
    if (!topo || topo->node_count < 2) return 0;

    uint32_t max_dist = 0;
    uint32_t dist[SPL_TOPO_MAX_NODES];
    int32_t prev[SPL_TOPO_MAX_NODES];

    for (uint32_t i = 0; i < topo->node_count; i++) {
        spl_topo_shortest_path(topo, topo->node_ids[i], dist, prev);
        for (uint32_t j = 0; j < topo->node_count; j++) {
            if (dist[j] != SPL_TOPO_INF && dist[j] > max_dist)
                max_dist = dist[j];
        }
    }

    return max_dist;
}

/*
 * Check if the topology is connected (all nodes reachable from any node).
 */
bool spl_topo_is_connected(const spl_topology_t *topo) {
    if (!topo || topo->node_count < 2) return true;

    uint32_t dist[SPL_TOPO_MAX_NODES];
    int32_t prev[SPL_TOPO_MAX_NODES];
    spl_topo_shortest_path(topo, topo->node_ids[0], dist, prev);

    for (uint32_t i = 1; i < topo->node_count; i++) {
        if (dist[i] == SPL_TOPO_INF) return false;
    }
    return true;
}
