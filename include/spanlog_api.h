/*
 * libspanlog public API declarations.
 * This header provides the complete public API surface.
 */
#ifndef SPL_API_H
#define SPL_API_H

#include "spanlog.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ═══════ Context & Session ═══════ */
spl_context_t *spl_context_create(void);
void           spl_context_destroy(spl_context_t *ctx);
spl_session_t *spl_session_create(spl_context_t *ctx, uint32_t capabilities);
void           spl_session_destroy(spl_session_t *sess);
uint32_t       spl_session_get_id(const spl_session_t *sess);
uint32_t       spl_session_get_epoch(const spl_session_t *sess);
uint32_t       spl_session_get_capabilities(const spl_session_t *sess);

/* ═══════ Protocol ═══════ */
int spl_protocol_parse_header(const uint8_t *data, size_t len,
                              spl_parsed_header_t *out);
int spl_protocol_parse_frame(const uint8_t *data, size_t len,
                             spl_parsed_frame_t *out);
int spl_ingest_frames(spl_session_t *sess, const uint8_t *data, size_t len);
int spl_frame_dispatch(spl_session_t *sess, const spl_parsed_frame_t *f);

/* ═══════ Span ═══════ */
spl_span_t *spl_span_registry_create(spl_session_t *sess, uint32_t span_id,
                                     uint32_t owner_token);
spl_span_t *spl_span_registry_lookup(spl_session_t *sess, uint32_t span_id);
int         spl_span_registry_destroy(spl_session_t *sess, spl_span_t *span);
int         spl_span_lifecycle_set_state(spl_span_t *span,
                                         spl_lifecycle_state_t state);
int         spl_span_lifecycle_incref(spl_span_t *span);
int         spl_span_lifecycle_decref(spl_session_t *sess, spl_span_t *span);
int         spl_span_lifecycle_deferred_destroy(spl_session_t *sess,
                                                spl_span_t *span);

/* ═══════ Segment ═══════ */
spl_segment_t *spl_segment_alloc(spl_session_t *sess, uint32_t span_id,
                                 size_t capacity);
int            spl_segment_write(spl_segment_t *seg, const uint8_t *data,
                                 size_t len);
int            spl_segment_realloc(spl_session_t *sess, spl_segment_t *seg,
                                   size_t new_capacity);
int            spl_segment_compact(spl_session_t *sess, spl_span_t *span);
int            spl_segment_gc_reclaim(spl_session_t *sess, spl_segment_t *seg);
int            spl_segment_gc_span(spl_session_t *sess, spl_span_t *span);

/* ═══════ Lease ═══════ */
spl_lease_t *spl_lease_acquire(spl_session_t *sess, uint32_t span_id,
                               uint32_t owner_token);
int          spl_lease_release(spl_session_t *sess, spl_lease_t *lease);
int          spl_lease_force_release(spl_session_t *sess, spl_lease_t *lease);
int          spl_lease_renew(spl_session_t *sess, spl_lease_t *lease);
spl_lease_t *spl_lease_lookup(spl_session_t *sess, uint32_t lease_id);
spl_lease_t *spl_lease_find_by_span(spl_session_t *sess, uint32_t span_id);

/* ═══════ Cursor ═══════ */
spl_cursor_t *spl_cursor_open(spl_session_t *sess, uint32_t span_id,
                              uint32_t lease_id);
int           spl_cursor_close(spl_session_t *sess, spl_cursor_t *cursor);
int           spl_cursor_advance_to_segment(spl_session_t *sess,
                                            spl_cursor_t *cursor,
                                            uint32_t segment_index);
int           spl_cursor_materialize(spl_session_t *sess,
                                     spl_cursor_t *cursor,
                                     uint8_t *out, size_t out_len,
                                     size_t *bytes_read);
spl_cursor_t *spl_cursor_lookup(spl_session_t *sess, uint32_t cursor_id);
int           spl_cursor_invalidate_for_span(spl_session_t *sess,
                                             uint32_t span_id);

/* ═══════ Channel ═══════ */
spl_channel_t *spl_channel_mux_alloc(spl_session_t *sess, uint32_t channel_id);
spl_channel_t *spl_channel_lookup(spl_session_t *sess, uint32_t channel_id);
int            spl_channel_bind_span(spl_session_t *sess, spl_channel_t *ch,
                                     uint32_t span_id, uint32_t partition_id);
int            spl_channel_dispatch_to_span(spl_session_t *sess,
                                            spl_channel_t *ch,
                                            const uint8_t *data, size_t len);
void           spl_session_gc_drained_channels(spl_session_t *sess);

/* ═══════ Stream ═══════ */
int spl_stream_open(spl_session_t *sess, uint32_t channel_id,
                    uint32_t stream_id);
int spl_stream_close(spl_session_t *sess, spl_stream_t *stream);
int spl_stream_buffer_append(spl_session_t *sess, spl_stream_t *stream,
                             const uint8_t *data, size_t len);
int spl_stream_flush_pending(spl_session_t *sess, spl_stream_t *stream);
spl_stream_t *spl_stream_lookup(spl_session_t *sess, uint32_t stream_id);

/* ═══════ Replication ═══════ */
int spl_repl_setup(spl_session_t *sess, uint32_t span_id, uint8_t quorum);
int spl_repl_add_member(spl_session_t *sess, uint32_t rs_id, uint32_t node_id);
int spl_repl_remove_member(spl_session_t *sess, uint32_t rs_id, uint32_t node_id);
int spl_repl_check_quorum(spl_session_t *sess, spl_replica_set_t *rs);
int spl_repl_snapshot_segments(spl_session_t *sess, spl_replica_set_t *rs);
spl_replica_set_t *spl_repl_lookup(spl_session_t *sess, uint32_t rs_id);
spl_replica_set_t *spl_repl_find_by_span(spl_session_t *sess, uint32_t span_id);

/* ═══════ Migration ═══════ */
int spl_migrate_initiate(spl_session_t *sess, uint32_t span_id,
                         uint32_t target_node);
int spl_migrate_abort(spl_session_t *sess, spl_migration_t *mig);
int spl_migrate_prepare_source(spl_session_t *sess, spl_migration_t *mig);
int spl_migrate_complete_handoff(spl_session_t *sess, spl_migration_t *mig);
spl_migration_t *spl_migrate_lookup(spl_session_t *sess, uint32_t id);
spl_migration_t *spl_migrate_find_by_span(spl_session_t *sess, uint32_t span_id);

/* ═══════ Route ═══════ */
int spl_route_advertise(spl_session_t *sess, uint32_t span_id,
                        uint32_t partition_id, uint32_t node_id);
int spl_route_add_node(spl_session_t *sess, uint32_t node_id,
                       const char *addr, uint16_t port);
int spl_route_remove_node(spl_session_t *sess, uint32_t node_id);
spl_route_entry_t *spl_route_lookup(spl_session_t *sess, uint32_t route_id);
spl_node_desc_t   *spl_node_lookup(spl_session_t *sess, uint32_t node_id);

/* ═══════ Partition ═══════ */
spl_partition_t *spl_partition_lookup(spl_session_t *sess, uint32_t pid);
int spl_partition_add(spl_session_t *sess, uint32_t pid,
                      uint32_t max_chunk, uint32_t window);
int spl_partition_remove(spl_session_t *sess, uint32_t pid);

/* ═══════ Backpressure ═══════ */
int spl_backpressure_add_partition(spl_session_t *sess, uint32_t pid,
                                  int32_t credits, int32_t max_credits);
int spl_backpressure_remove_partition(spl_session_t *sess, uint32_t pid);
int spl_backpressure_apply_credits(spl_session_t *sess, uint32_t pid,
                                   int32_t delta);

/* ═══════ Checkpoint ═══════ */
int spl_checkpoint_begin(spl_session_t *sess, uint32_t span_id,
                         uint32_t ckpt_id);
int spl_checkpoint_flush(spl_session_t *sess, spl_checkpoint_t *ckpt);
int spl_checkpoint_restore(spl_session_t *sess, spl_checkpoint_t *ckpt);
int spl_checkpoint_cleanup_listener(spl_session_t *sess,
                                    spl_checkpoint_t *ckpt);
spl_checkpoint_t *spl_checkpoint_lookup(spl_session_t *sess, uint32_t id);

/* ═══════ Utilities ═══════ */
uint32_t spl_crc32c(const uint8_t *data, size_t len);
uint32_t spl_fnv1a(const uint8_t *data, size_t len);
uint32_t spl_hash_mix(uint32_t a, uint32_t b);
uint32_t spl_next_power_of_2(uint32_t v);
bool     spl_is_power_of_2(uint32_t v);

void     spl_write_u16(uint8_t *buf, uint16_t val);
void     spl_write_u32(uint8_t *buf, uint32_t val);
void     spl_write_u64(uint8_t *buf, uint64_t val);
uint16_t spl_read_u16(const uint8_t *buf);
uint32_t spl_read_u32(const uint8_t *buf);
uint64_t spl_read_u64(const uint8_t *buf);

#endif /* SPL_API_H */

/*
 * Channel multiplexing functions.
 *
 * Channels provide logical multiplexing within a session,
 * allowing concurrent operations on different spans through
 * a single session. Each channel is bound to a span and
 * partition for data operations.
 *
 * Channel lifecycle:
 *   open   - allocate channel with unique ID
 *   bind   - associate with span and partition
 *   remap  - change partition binding
 *   drain  - stop accepting new operations
 *   close  - release channel resources
 *
 * Channels support backpressure through credit-based flow
 * control. Each channel's partition has a credit balance
 * that limits the rate of operations.
 */

/*
 * Stream functions.
 *
 * Streams provide ordered byte-stream abstraction over
 * channel-multiplexed segment access. Streams handle
 * fragment reassembly, buffered appends, and flushing.
 *
 * Stream operations:
 *   open     - create stream on a channel
 *   append   - buffer data for writing to span segments
 *   flush    - write buffered data to target segment
 *   read     - materialize data through cursor
 *   close    - release stream resources
 */

/*
 * Replication functions.
 *
 * Replication provides data durability through replica sets.
 * Each replica set maintains copies of span data across
 * multiple nodes with quorum-based consistency.
 *
 * Operations:
 *   setup    - create replica set for a span
 *   add/remove member - manage replica set membership
 *   quorum check - verify sufficient replicas are alive
 *   sync     - snapshot and transfer segment data
 *   health   - monitor replica health and detect split-brain
 */

/*
 * Migration functions.
 *
 * Migration orchestrates live span transfer between nodes
 * for cluster rebalancing. The migration protocol:
 *   1. initiate  - mark span for migration, assign target
 *   2. prepare   - reallocate segments with transfer headers
 *   3. transfer  - stream segment data to target node
 *   4. handoff   - transfer ownership, free source segments
 *   5. complete  - finalize migration, update routes
 *
 * Migrations can be aborted at any phase before handoff.
 */

/*
 * Checkpoint functions.
 *
 * Checkpoints provide consistent snapshots of span state
 * for durability and recovery. A checkpoint captures
 * segment pointers at a specific generation.
 *
 * Checkpoint lifecycle:
 *   begin    - snapshot current segment state
 *   flush    - serialize snapshotted segments
 *   restore  - recover segment state from checkpoint
 */

/*
 * Retention and backpressure functions.
 *
 * Retention manages automatic cleanup of old segment data
 * based on configurable policies (age, size thresholds).
 *
 * Backpressure provides credit-based flow control
 * per partition, using AIMD congestion control.
 */

/*
 * Route and partition functions.
 *
 * Routes map spans to nodes and partitions.
 * Partitions define data distribution boundaries
 * with configurable chunk sizes and flow control windows.
 *
 * The route table supports consistent hashing for
 * balanced partition-to-node mapping, with automatic
 * rebalancing when nodes join or depart the cluster.
 * Route metrics track per-route latency, throughput,
 * and error rates for intelligent routing decisions.
 */
