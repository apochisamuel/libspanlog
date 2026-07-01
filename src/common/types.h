/*
 * Internal type definitions shared across subsystems
 */
#ifndef SPL_TYPES_H
#define SPL_TYPES_H

#include "spanlog.h"
#include "util.h"
#include "bitops.h"
#include "crc.h"
#include "alloc.h"
#include <string.h>
#include <stdlib.h>

/* ── Wire format structures ──────────────────────────────────────── */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t session_id;
    uint32_t epoch;
    uint16_t frame_count;
    uint32_t header_crc;
} __attribute__((packed)) spl_wire_header_t;

#define SPL_WIRE_HEADER_SIZE 20

typedef struct {
    uint16_t length;
    uint8_t  frame_type;
    uint16_t sequence;
    uint32_t session_id;
    uint16_t channel_id;
    uint16_t route_id;
    uint32_t owner_token;
    uint16_t flags;
    uint8_t  subrecord_count;
    uint32_t payload_crc;
} __attribute__((packed)) spl_wire_frame_t;

#define SPL_WIRE_FRAME_HEADER_SIZE 20

typedef struct {
    uint8_t  kind;
    uint16_t length;
    uint32_t local_epoch;
    uint32_t generation;
    uint32_t lease_token;
    /* payload follows */
} __attribute__((packed)) spl_wire_subrecord_t;

#define SPL_WIRE_SUBRECORD_HEADER_SIZE 15

/* ── Parsed frame representation ─────────────────────────────────── */

typedef struct {
    uint8_t          kind;
    uint16_t         length;
    uint32_t         local_epoch;
    uint32_t         generation;
    uint32_t         lease_token;
    const uint8_t   *payload;
    uint16_t         payload_len;
} spl_subrecord_t;

typedef struct {
    uint16_t         total_length;
    uint8_t          frame_type;
    uint16_t         sequence;
    uint32_t         session_id;
    uint16_t         channel_id;
    uint16_t         route_id;
    uint32_t         owner_token;
    uint16_t         flags;
    uint8_t          subrecord_count;
    uint32_t         payload_crc;
    spl_subrecord_t  subrecords[SPL_MAX_SUBRECORDS];
    uint8_t          parsed_subrecord_count;
} spl_parsed_frame_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t session_id;
    uint32_t epoch;
    uint16_t frame_count;
    uint32_t header_crc;
} spl_parsed_header_t;

/* ── Lifecycle states ────────────────────────────────────────────── */

typedef enum {
    SPL_LIFECYCLE_CREATED    = 0,
    SPL_LIFECYCLE_ACTIVE     = 1,
    SPL_LIFECYCLE_DRAINING   = 2,
    SPL_LIFECYCLE_MIGRATING  = 3,
    SPL_LIFECYCLE_CLOSING    = 4,
    SPL_LIFECYCLE_CLOSED     = 5,
    SPL_LIFECYCLE_DEFERRED   = 6,
    SPL_LIFECYCLE_FREED      = 7
} spl_lifecycle_state_t;

/* ── Span metadata ───────────────────────────────────────────────── */

typedef struct spl_segment spl_segment_t;

typedef struct spl_span {
    uint32_t             span_id;
    uint32_t             owner_token;
    uint32_t             generation;
    uint32_t             epoch_created;
    spl_lifecycle_state_t state;
    int32_t              refcount;

    spl_segment_t       *segments[SPL_MAX_SEGMENTS];
    uint16_t             segment_count;

    uint32_t             route_id;
    uint32_t             partition_id;

    /* migration tracking */
    uint32_t             migration_id;
    bool                 migration_source;

    /* retention */
    uint64_t             retention_watermark;
    uint64_t             total_bytes;

    /* replication */
    uint32_t             replica_set_id;
} spl_span_t;

/* ── Segment ─────────────────────────────────────────────────────── */

struct spl_segment {
    uint32_t         segment_id;
    uint32_t         span_id;
    uint32_t         generation;
    uint8_t         *data;
    size_t           capacity;
    size_t           used;
    spl_lifecycle_state_t state;
    bool             compacted;
};

/* ── Lease ───────────────────────────────────────────────────────── */

typedef struct {
    uint32_t         lease_id;
    uint32_t         span_id;
    uint32_t         owner_token;
    uint32_t         epoch_acquired;
    uint32_t         generation;
    uint32_t         renewal_count;
    bool             active;
    bool             force_released;
} spl_lease_t;

/* ── Cursor ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t         cursor_id;
    uint32_t         span_id;
    uint32_t         lease_id;
    uint32_t         generation;
    uint32_t         position;
    uint32_t         segment_index;
    uint8_t         *cached_segment_data;   /* direct pointer into segment data */
    size_t           cached_data_len;
    bool             active;
    bool             invalidated;
} spl_cursor_t;

/* ── Channel ─────────────────────────────────────────────────────── */

typedef struct spl_channel {
    uint32_t         channel_id;
    uint32_t         session_id;
    uint32_t         span_id;
    uint32_t         partition_id;
    uint32_t         lease_id;
    spl_lifecycle_state_t state;
    spl_span_t      *cached_span;       /* cached resolved span pointer */

    /* backpressure */
    void            *bp_callback_ctx;
    uint32_t         bp_partition_credit_idx;

    /* inflight */
    uint32_t         inflight_count;
    uint32_t         inflight_ops[64];
} spl_channel_t;

/* ── Stream ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t         stream_id;
    uint32_t         channel_id;
    uint32_t         span_id;
    uint32_t         cursor_id;
    bool             active;
    spl_lifecycle_state_t state;

    /* reassembly */
    uint8_t         *reassembly_buf;
    size_t           reassembly_capacity;
    size_t           reassembly_used;
    uint32_t         reassembly_expected_seq;
    uint32_t         partition_max_chunk;

    /* pending flush */
    uint8_t         *pending_data;
    size_t           pending_len;
    spl_segment_t   *pending_target_segment;

    /* crypto context caching */
    uint8_t         *cached_crypto_ctx;
    size_t           cached_crypto_ctx_len;
    uint32_t         crypto_ctx_generation;
} spl_stream_t;

/* ── Replication ─────────────────────────────────────────────────── */

typedef struct {
    uint32_t         node_id;
    char             addr[64];
    uint16_t         port;
    bool             active;
} spl_node_desc_t;

typedef struct {
    uint32_t         replica_set_id;
    uint32_t         span_id;
    spl_node_desc_t *members[SPL_MAX_REPLICAS];
    uint8_t          member_count;
    uint8_t          quorum_size;
    bool             active;

    /* snapshot for sync */
    spl_span_t      *snap_span;
    spl_segment_t  **snap_segments;
    uint16_t         snap_segment_count;
    uint32_t         snap_generation;
    bool             snap_active;
} spl_replica_set_t;

/* ── Migration ───────────────────────────────────────────────────── */

typedef enum {
    SPL_MIGRATE_IDLE        = 0,
    SPL_MIGRATE_INITIATED   = 1,
    SPL_MIGRATE_PREPARING   = 2,
    SPL_MIGRATE_TRANSFERRING = 3,
    SPL_MIGRATE_HANDOFF     = 4,
    SPL_MIGRATE_COMPLETE    = 5,
    SPL_MIGRATE_ABORTED     = 6
} spl_migrate_phase_t;

typedef struct {
    uint32_t             migration_id;
    uint32_t             span_id;
    uint32_t             source_node_id;
    uint32_t             target_node_id;
    spl_migrate_phase_t  phase;
    uint32_t             epoch_started;
    uint32_t             owner_token;
    bool                 active;
    uint64_t             bytes_transferred;
    uint64_t             total_bytes;
} spl_migration_t;

/* ── Checkpoint ──────────────────────────────────────────────────── */

typedef enum {
    SPL_CKPT_IDLE       = 0,
    SPL_CKPT_IN_PROGRESS = 1,
    SPL_CKPT_FLUSHING   = 2,
    SPL_CKPT_RESTORING  = 3,
    SPL_CKPT_COMPLETE   = 4
} spl_ckpt_phase_t;

typedef struct {
    uint32_t         checkpoint_id;
    uint32_t         span_id;
    spl_ckpt_phase_t phase;
    uint32_t         epoch_started;
    uint32_t         generation_at_start;

    /* snapshot of segment pointers */
    spl_segment_t   *snap_segments[SPL_MAX_SEGMENTS];
    uint16_t         snap_segment_count;

    /* listener for cleanup */
    uint32_t         listener_lease_id;   /* cached lease ID from registration */
    bool             listener_active;

    bool             active;
} spl_checkpoint_t;

/* ── Retention ───────────────────────────────────────────────────── */

typedef struct {
    uint64_t         watermark_position;
    uint32_t         span_id;
    uint64_t         gc_threshold_bytes;
    uint32_t         gc_eligible_count;
    bool             active;
} spl_retention_state_t;

/* ── Route ───────────────────────────────────────────────────────── */

typedef struct {
    uint32_t         route_id;
    uint32_t         span_id;
    uint32_t         partition_id;
    uint32_t         node_id;
    spl_span_t      *resolved_span;  /* cached span pointer */
    uint32_t         generation;
    bool             active;
} spl_route_entry_t;

/* ── Partition ───────────────────────────────────────────────────── */

typedef struct {
    uint32_t         partition_id;
    uint32_t         max_chunk_size;
    uint32_t         credit_balance;
    uint32_t         window_size;
    bool             active;
} spl_partition_t;

/* ── Backpressure ────────────────────────────────────────────────── */

typedef void (*spl_bp_callback_fn)(void *ctx, uint32_t partition_id, int32_t credits);

typedef struct {
    uint32_t          partition_id;
    int32_t           credits;
    int32_t           max_credits;
    uint32_t          window_size;
    spl_bp_callback_fn callback;
    void              *callback_ctx;
    bool              active;
} spl_bp_partition_entry_t;

/* ── Session (internal) ──────────────────────────────────────────── */

typedef struct spl_session {
    uint32_t             session_id;
    uint32_t             epoch;
    uint32_t             capabilities;
    uint16_t             next_sequence;
    spl_lifecycle_state_t state;
    bool                 initialized;
    bool                 negotiated;

    /* crypto context */
    uint8_t             *crypto_ctx;
    size_t               crypto_ctx_len;
    uint32_t             crypto_generation;

    /* registries */
    spl_span_t          *spans[SPL_MAX_SPANS];
    uint16_t             span_count;

    spl_segment_t       *segments[SPL_MAX_SEGMENTS];
    uint16_t             segment_count;

    spl_channel_t       *channels[SPL_MAX_CHANNELS];
    uint16_t             channel_count;

    spl_stream_t        *streams[SPL_MAX_STREAMS];
    uint16_t             stream_count;

    spl_cursor_t        *cursors[SPL_MAX_CURSORS];
    uint16_t             cursor_count;

    spl_lease_t         *leases[SPL_MAX_LEASES];
    uint16_t             lease_count;

    spl_replica_set_t   *replica_sets[SPL_MAX_REPLICAS];
    uint8_t              replica_set_count;

    spl_migration_t     *migrations[SPL_MAX_MIGRATIONS];
    uint8_t              migration_count;

    spl_checkpoint_t    *checkpoints[SPL_MAX_CHECKPOINTS];
    uint8_t              checkpoint_count;

    spl_retention_state_t retention[SPL_MAX_SPANS];

    spl_route_entry_t   *routes[SPL_MAX_ROUTES];
    uint16_t             route_count;

    spl_node_desc_t     *nodes[SPL_MAX_NODES];
    uint8_t              node_count;

    spl_partition_t     *partitions[SPL_MAX_PARTITIONS];
    uint8_t              partition_count;

    spl_bp_partition_entry_t *bp_entries[SPL_MAX_PARTITIONS];
    uint8_t              bp_entry_count;

    /* deferred cleanup */
    spl_channel_t       *drain_list[SPL_MAX_CHANNELS];
    uint16_t             drain_count;

    spl_span_t          *deferred_delete_list[SPL_MAX_SPANS];
    uint16_t             deferred_delete_count;
    uint32_t             deferred_delete_grace;

    /* context back-pointer */
    spl_context_t       *ctx;
} spl_session_t;

/* ── Context ─────────────────────────────────────────────────────── */

struct spl_context {
    spl_session_t *sessions[SPL_MAX_SESSIONS];
    uint8_t        session_count;
    uint32_t       next_id;
};

/* ── Internal forward declarations ───────────────────────────────── */

/* protocol */
int  spl_protocol_parse_header(const uint8_t *data, size_t len,
                               spl_parsed_header_t *out);
int  spl_protocol_parse_frame(const uint8_t *data, size_t len,
                              spl_parsed_frame_t *out);
uint32_t spl_crc32c(const uint8_t *data, size_t len);
int  spl_protocol_validate_frame_crc(const uint8_t *data, size_t frame_len,
                                     uint32_t expected_crc);
int  spl_frame_dispatch(spl_session_t *sess, const spl_parsed_frame_t *frame);

/* session */
int  spl_session_handle_init(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_session_handle_cap_neg(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_session_advance_epoch(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_session_rotate_context(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_session_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_session_gc_drained_channels(spl_session_t *sess);
int  spl_session_invalidate_epoch_leases(spl_session_t *sess, uint32_t old_epoch);

/* span */
spl_span_t *spl_span_registry_create(spl_session_t *sess, uint32_t span_id,
                                     uint32_t owner_token);
spl_span_t *spl_span_registry_lookup(spl_session_t *sess, uint32_t span_id);
int  spl_span_registry_destroy(spl_session_t *sess, spl_span_t *span);
int  spl_span_lifecycle_incref(spl_span_t *span);
int  spl_span_lifecycle_decref(spl_session_t *sess, spl_span_t *span);
int  spl_span_lifecycle_bump_generation(spl_span_t *span);
int  spl_span_lifecycle_set_state(spl_span_t *span, spl_lifecycle_state_t st);
int  spl_span_lifecycle_deferred_destroy(spl_session_t *sess, spl_span_t *span);
int  spl_span_handle_create(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_span_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_span_handle_meta_update(spl_session_t *sess, const spl_parsed_frame_t *f);

/* segment */
spl_segment_t *spl_segment_alloc(spl_session_t *sess, uint32_t span_id,
                                 size_t capacity);
int  spl_segment_free(spl_session_t *sess, spl_segment_t *seg);
int  spl_segment_realloc(spl_session_t *sess, spl_segment_t *seg,
                         size_t new_capacity);
int  spl_segment_compact(spl_session_t *sess, spl_span_t *span);
int  spl_segment_gc_reclaim(spl_session_t *sess, spl_segment_t *seg);
int  spl_segment_gc_span(spl_session_t *sess, spl_span_t *span);
int  spl_segment_write(spl_segment_t *seg, const uint8_t *data, size_t len);
int  spl_segment_handle_attach(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_segment_handle_detach(spl_session_t *sess, const spl_parsed_frame_t *f);

/* channel */
spl_channel_t *spl_channel_mux_alloc(spl_session_t *sess, uint32_t channel_id);
int  spl_channel_mux_free(spl_session_t *sess, spl_channel_t *ch);
int  spl_channel_bind_span(spl_session_t *sess, spl_channel_t *ch,
                           uint32_t span_id, uint32_t partition_id);
int  spl_channel_dispatch_to_span(spl_session_t *sess, spl_channel_t *ch,
                                  const uint8_t *data, size_t len);
int  spl_channel_remap_partition(spl_session_t *sess, spl_channel_t *ch,
                                 uint32_t new_partition_id);
int  spl_channel_handle_open(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_channel_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_channel_handle_bind(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_channel_handle_remap(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_channel_handle_drain(spl_session_t *sess, const spl_parsed_frame_t *f);
spl_channel_t *spl_channel_lookup(spl_session_t *sess, uint32_t channel_id);

/* lease */
spl_lease_t *spl_lease_acquire(spl_session_t *sess, uint32_t span_id,
                               uint32_t owner_token);
int  spl_lease_release(spl_session_t *sess, spl_lease_t *lease);
int  spl_lease_renew(spl_session_t *sess, spl_lease_t *lease);
int  spl_lease_force_release(spl_session_t *sess, spl_lease_t *lease);
spl_lease_t *spl_lease_lookup(spl_session_t *sess, uint32_t lease_id);
spl_lease_t *spl_lease_find_by_span(spl_session_t *sess, uint32_t span_id);
int  spl_lease_handle_acquire(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_lease_handle_release(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_lease_handle_renew(spl_session_t *sess, const spl_parsed_frame_t *f);

/* cursor */
spl_cursor_t *spl_cursor_open(spl_session_t *sess, uint32_t span_id,
                              uint32_t lease_id);
int  spl_cursor_close(spl_session_t *sess, spl_cursor_t *cursor);
int  spl_cursor_advance_to_segment(spl_session_t *sess, spl_cursor_t *cursor,
                                   uint32_t segment_index);
int  spl_cursor_materialize(spl_session_t *sess, spl_cursor_t *cursor,
                            uint8_t *out, size_t out_len, size_t *bytes_read);
int  spl_cursor_invalidate_for_span(spl_session_t *sess, uint32_t span_id);
int  spl_cursor_handle_open(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_cursor_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_cursor_handle_advance(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_cursor_handle_read(spl_session_t *sess, const spl_parsed_frame_t *f);
spl_cursor_t *spl_cursor_lookup(spl_session_t *sess, uint32_t cursor_id);

/* stream */
int  spl_stream_open(spl_session_t *sess, uint32_t channel_id, uint32_t stream_id);
int  spl_stream_close(spl_session_t *sess, spl_stream_t *stream);
int  spl_stream_buffer_append(spl_session_t *sess, spl_stream_t *stream,
                              const uint8_t *data, size_t len);
int  spl_stream_materialize_chunk(spl_session_t *sess, spl_stream_t *stream,
                                  uint8_t *out, size_t out_len,
                                  size_t *bytes_read);
int  spl_stream_flush_pending(spl_session_t *sess, spl_stream_t *stream);
int  spl_stream_reassemble_fragment(spl_session_t *sess, spl_stream_t *stream,
                                    const uint8_t *frag, size_t frag_len,
                                    uint32_t seq);
int  spl_stream_validate_fragment(spl_session_t *sess, spl_stream_t *stream,
                                  const uint8_t *frag, size_t frag_len);
int  spl_stream_handle_open(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_stream_handle_close(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_stream_handle_append(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_stream_handle_read(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_stream_handle_flush(spl_session_t *sess, const spl_parsed_frame_t *f);
spl_stream_t *spl_stream_lookup(spl_session_t *sess, uint32_t stream_id);

/* replication */
int  spl_repl_setup(spl_session_t *sess, uint32_t span_id, uint8_t quorum);
int  spl_repl_add_member(spl_session_t *sess, uint32_t replica_set_id,
                         uint32_t node_id);
int  spl_repl_remove_member(spl_session_t *sess, uint32_t replica_set_id,
                            uint32_t node_id);
int  spl_repl_snapshot_segments(spl_session_t *sess, spl_replica_set_t *rs);
int  spl_repl_snapshot_replica_set(spl_session_t *sess, spl_replica_set_t *rs);
int  spl_repl_serialize_segment(spl_session_t *sess, spl_replica_set_t *rs,
                                uint32_t seg_idx);
int  spl_repl_check_quorum(spl_session_t *sess, spl_replica_set_t *rs);
int  spl_repl_validate_replica_set(spl_session_t *sess, spl_replica_set_t *rs);
int  spl_repl_handle_setup(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_repl_handle_add_member(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_repl_handle_remove_member(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_repl_handle_sync_begin(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_repl_handle_sync_data(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_repl_handle_check(spl_session_t *sess, const spl_parsed_frame_t *f);
spl_replica_set_t *spl_repl_lookup(spl_session_t *sess, uint32_t replica_set_id);
spl_replica_set_t *spl_repl_find_by_span(spl_session_t *sess, uint32_t span_id);

/* migration */
int  spl_migrate_initiate(spl_session_t *sess, uint32_t span_id,
                          uint32_t target_node);
int  spl_migrate_prepare_source(spl_session_t *sess, spl_migration_t *mig);
int  spl_migrate_begin_transfer(spl_session_t *sess, spl_migration_t *mig);
int  spl_migrate_complete_handoff(spl_session_t *sess, spl_migration_t *mig);
int  spl_migrate_finalize_source(spl_session_t *sess, spl_migration_t *mig);
int  spl_migrate_abort(spl_session_t *sess, spl_migration_t *mig);
int  spl_migrate_handle_initiate(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_migrate_handle_prepare(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_migrate_handle_transfer(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_migrate_handle_handoff(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_migrate_handle_abort(spl_session_t *sess, const spl_parsed_frame_t *f);
spl_migration_t *spl_migrate_lookup(spl_session_t *sess, uint32_t migration_id);
spl_migration_t *spl_migrate_find_by_span(spl_session_t *sess, uint32_t span_id);

/* checkpoint */
int  spl_checkpoint_begin(spl_session_t *sess, uint32_t span_id, uint32_t ckpt_id);
int  spl_checkpoint_flush(spl_session_t *sess, spl_checkpoint_t *ckpt);
int  spl_checkpoint_serialize_segments(spl_session_t *sess, spl_checkpoint_t *ckpt);
int  spl_checkpoint_restore(spl_session_t *sess, spl_checkpoint_t *ckpt);
int  spl_checkpoint_register_listener(spl_session_t *sess, spl_checkpoint_t *ckpt,
                                      uint32_t lease_id);
int  spl_checkpoint_cleanup_listener(spl_session_t *sess, spl_checkpoint_t *ckpt);
int  spl_checkpoint_handle_begin(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_checkpoint_handle_flush(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_checkpoint_handle_restore(spl_session_t *sess, const spl_parsed_frame_t *f);
spl_checkpoint_t *spl_checkpoint_lookup(spl_session_t *sess, uint32_t ckpt_id);

/* retention */
int  spl_retention_evaluate_watermark(spl_session_t *sess, uint32_t span_id);
int  spl_retention_gc_collect(spl_session_t *sess, uint32_t span_id);
int  spl_retention_gc_eligible(spl_session_t *sess, spl_span_t *span,
                               spl_segment_t **out_eligible, uint16_t *count);
int  spl_retention_handle_eval(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_retention_handle_collect(spl_session_t *sess, const spl_parsed_frame_t *f);

/* route */
int  spl_route_advertise(spl_session_t *sess, uint32_t span_id,
                         uint32_t partition_id, uint32_t node_id);
int  spl_route_withdraw(spl_session_t *sess, uint32_t route_id);
spl_span_t *spl_route_resolve_span(spl_session_t *sess, uint32_t route_id);
int  spl_route_update_mapping(spl_session_t *sess, uint32_t route_id,
                              uint32_t new_span_id);
int  spl_route_add_node(spl_session_t *sess, uint32_t node_id,
                        const char *addr, uint16_t port);
int  spl_route_remove_node(spl_session_t *sess, uint32_t node_id);
int  spl_route_handle_advertise(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_route_handle_withdraw(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_route_handle_rebalance(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_route_handle_node_join(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_route_handle_node_depart(spl_session_t *sess, const spl_parsed_frame_t *f);
spl_route_entry_t *spl_route_lookup(spl_session_t *sess, uint32_t route_id);
spl_node_desc_t *spl_node_lookup(spl_session_t *sess, uint32_t node_id);

/* partition */
int  spl_partition_add(spl_session_t *sess, uint32_t partition_id,
                       uint32_t max_chunk, uint32_t window_size);
int  spl_partition_remove(spl_session_t *sess, uint32_t partition_id);
spl_partition_t *spl_partition_lookup(spl_session_t *sess, uint32_t partition_id);
int  spl_partition_handle_add(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_partition_handle_remove(spl_session_t *sess, const spl_parsed_frame_t *f);

/* backpressure */
int  spl_backpressure_add_partition(spl_session_t *sess, uint32_t partition_id,
                                   int32_t initial_credits, int32_t max_credits);
int  spl_backpressure_remove_partition(spl_session_t *sess, uint32_t partition_id);
int  spl_backpressure_register_callback(spl_session_t *sess, uint32_t partition_id,
                                        spl_bp_callback_fn fn, void *ctx);
int  spl_backpressure_apply_credits(spl_session_t *sess, uint32_t partition_id,
                                    int32_t delta);
int  spl_backpressure_debit_partition(spl_session_t *sess, uint32_t partition_id,
                                     int32_t amount);
int  spl_backpressure_handle_credit(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_backpressure_handle_window(spl_session_t *sess, const spl_parsed_frame_t *f);

/* drain/gc */
int  spl_handle_drain_deferred(spl_session_t *sess, const spl_parsed_frame_t *f);
int  spl_handle_gc_trigger(spl_session_t *sess, const spl_parsed_frame_t *f);

#endif /* SPL_TYPES_H */

/* FIX: Epoch-aware cursor invalidation */
int spl_cursor_invalidate_for_epoch(spl_session_t *sess, uint32_t span_id,
                                    uint32_t old_epoch, uint32_t new_epoch);
/* FIX: Stream span invalidation */
int spl_stream_invalidate_for_span(spl_session_t *sess, uint32_t span_id);
