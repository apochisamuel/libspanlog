#include "../common/types.h"
#include "../common/alloc.h"

static uint32_t next_session_id = 1;

spl_context_t *spl_context_create(void) {
    spl_context_t *ctx = spl_calloc(1, sizeof(spl_context_t));
    if (!ctx) return NULL;
    ctx->next_id = 1000;
    return ctx;
}

void spl_context_destroy(spl_context_t *ctx) {
    if (!ctx) return;
    for (uint8_t i = 0; i < ctx->session_count; i++) {
        if (ctx->sessions[i])
            spl_session_destroy(ctx->sessions[i]);
    }
    spl_free(ctx);
}

spl_session_t *spl_session_create(spl_context_t *ctx, uint32_t capabilities) {
    if (!ctx) return NULL;
    if (ctx->session_count >= SPL_MAX_SESSIONS) return NULL;

    spl_session_t *sess = spl_calloc(1, sizeof(spl_session_t));
    if (!sess) return NULL;

    sess->session_id = next_session_id++;
    sess->epoch = 0;
    sess->capabilities = capabilities;
    sess->next_sequence = 0;
    sess->state = SPL_LIFECYCLE_CREATED;
    sess->initialized = false;
    sess->negotiated = false;
    sess->ctx = ctx;
    sess->deferred_delete_grace = 3;

    ctx->sessions[ctx->session_count++] = sess;
    return sess;
}

void spl_session_destroy(spl_session_t *sess) {
    if (!sess) return;

    /* Destroy in dependency order: cursors, streams, channels, leases,
       checkpoints, migrations, replicas, spans, segments, routes,
       partitions, backpressure, nodes */

    for (uint16_t i = 0; i < sess->cursor_count; i++) {
        if (sess->cursors[i]) {
            spl_free(sess->cursors[i]);
            sess->cursors[i] = NULL;
        }
    }
    for (uint16_t i = 0; i < sess->stream_count; i++) {
        if (sess->streams[i]) {
            spl_free(sess->streams[i]->reassembly_buf);
            spl_free(sess->streams[i]->pending_data);
            spl_free(sess->streams[i]);
            sess->streams[i] = NULL;
        }
    }
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i]) {
            spl_free(sess->channels[i]);
            sess->channels[i] = NULL;
        }
    }
    for (uint16_t i = 0; i < sess->lease_count; i++) {
        if (sess->leases[i]) {
            spl_free(sess->leases[i]);
            sess->leases[i] = NULL;
        }
    }
    for (uint8_t i = 0; i < sess->checkpoint_count; i++) {
        if (sess->checkpoints[i]) {
            spl_free(sess->checkpoints[i]);
            sess->checkpoints[i] = NULL;
        }
    }
    for (uint8_t i = 0; i < sess->migration_count; i++) {
        if (sess->migrations[i]) {
            spl_free(sess->migrations[i]);
            sess->migrations[i] = NULL;
        }
    }
    for (uint8_t i = 0; i < sess->replica_set_count; i++) {
        if (sess->replica_sets[i]) {
            spl_free(sess->replica_sets[i]->snap_segments);
            spl_free(sess->replica_sets[i]);
            sess->replica_sets[i] = NULL;
        }
    }
    for (uint16_t i = 0; i < sess->span_count; i++) {
        if (sess->spans[i]) {
            spl_free(sess->spans[i]);
            sess->spans[i] = NULL;
        }
    }
    for (uint16_t i = 0; i < sess->segment_count; i++) {
        if (sess->segments[i]) {
            spl_free(sess->segments[i]->data);
            spl_free(sess->segments[i]);
            sess->segments[i] = NULL;
        }
    }
    for (uint16_t i = 0; i < sess->route_count; i++) {
        spl_free(sess->routes[i]);
        sess->routes[i] = NULL;
    }
    for (uint8_t i = 0; i < sess->node_count; i++) {
        spl_free(sess->nodes[i]);
        sess->nodes[i] = NULL;
    }
    for (uint8_t i = 0; i < sess->partition_count; i++) {
        spl_free(sess->partitions[i]);
        sess->partitions[i] = NULL;
    }
    for (uint8_t i = 0; i < sess->bp_entry_count; i++) {
        spl_free(sess->bp_entries[i]);
        sess->bp_entries[i] = NULL;
    }

    /* Clean drain/deferred lists (already freed above) */
    sess->drain_count = 0;
    sess->deferred_delete_count = 0;

    spl_free(sess->crypto_ctx);
    spl_free(sess);
}

int spl_session_handle_init(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (sess->initialized) return SPL_ERR_INVALID_STATE;

    /* Accept session_id from frame */
    if (f->session_id != 0)
        sess->session_id = f->session_id;

    sess->epoch = 0;
    sess->state = SPL_LIFECYCLE_ACTIVE;
    sess->initialized = true;
    sess->next_sequence = f->sequence;
    return SPL_OK;
}

int spl_session_handle_cap_neg(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->initialized) return SPL_ERR_INVALID_STATE;
    if (sess->negotiated) return SPL_ERR_INVALID_STATE;

    /* Extract capabilities from first subrecord payload */
    if (f->parsed_subrecord_count > 0 && f->subrecords[0].payload_len >= 4) {
        uint32_t requested = spl_read_u32(f->subrecords[0].payload);
        sess->capabilities &= requested;
    }

    /* Initialize crypto context if crypto capability */
    if (sess->capabilities & SPL_CAP_CRYPTO) {
        sess->crypto_ctx_len = 64;
        sess->crypto_ctx = spl_calloc(1, sess->crypto_ctx_len);
        if (!sess->crypto_ctx) return SPL_ERR_NOMEM;
        sess->crypto_generation = 1;
        /* Fill with deterministic key material based on session_id */
        for (size_t i = 0; i < sess->crypto_ctx_len; i++) {
            sess->crypto_ctx[i] = (uint8_t)((sess->session_id + i * 37) & 0xFF);
        }
    }

    sess->negotiated = true;
    return SPL_OK;
}

int spl_session_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f) {
    (void)f;
    if (!sess) return SPL_ERR_INVALID_FRAME;
    sess->state = SPL_LIFECYCLE_CLOSED;
    return SPL_OK;
}

int spl_session_get_channel_count(const spl_session_t *sess) {
    if (!sess) return 0;
    return sess->channel_count;
}

int spl_session_get_span_count(const spl_session_t *sess) {
    if (!sess) return 0;
    return sess->span_count;
}

uint32_t spl_session_get_epoch(const spl_session_t *sess) {
    if (!sess) return 0;
    return sess->epoch;
}

uint32_t spl_session_get_id(const spl_session_t *sess) {
    if (!sess) return 0;
    return sess->session_id;
}

uint32_t spl_session_get_capabilities(const spl_session_t *sess) {
    if (!sess) return 0;
    return sess->capabilities;
}

const char *spl_version_string(void) {
    return "1.4.0";
}

uint32_t spl_version_numeric(void) {
    return (SPL_VERSION_MAJOR << 16) | (SPL_VERSION_MINOR << 8) | SPL_VERSION_PATCH;
}
