/*
 * libspanlog — Binary coordination protocol for distributed log segment management
 *
 * Copyright (c) 2024-2025 Samuel Apochi and Marcus Chen
 * SPDX-License-Identifier: MIT
 */

#ifndef SPANLOG_H
#define SPANLOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ─────────────────────────────────────────────────────── */
#define SPL_VERSION_MAJOR 1
#define SPL_VERSION_MINOR 4
#define SPL_VERSION_PATCH 0

/* ── Limits ──────────────────────────────────────────────────────── */
#define SPL_MAX_SESSIONS        64
#define SPL_MAX_CHANNELS        256
#define SPL_MAX_SPANS           512
#define SPL_MAX_SEGMENTS        2048
#define SPL_MAX_CURSORS         256
#define SPL_MAX_LEASES          512
#define SPL_MAX_STREAMS         256
#define SPL_MAX_PARTITIONS      128
#define SPL_MAX_ROUTES          512
#define SPL_MAX_REPLICAS        16
#define SPL_MAX_NODES           64
#define SPL_MAX_CHECKPOINTS     32
#define SPL_MAX_MIGRATIONS      32
#define SPL_MAX_SUBRECORDS      16
#define SPL_MAX_FRAME_SIZE      65536
#define SPL_MAX_SEGMENT_SIZE    (1 << 20)
#define SPL_MAGIC               0x53504C47  /* "SPLG" */

/* ── Error codes ─────────────────────────────────────────────────── */
typedef enum {
    SPL_OK = 0,
    SPL_ERR_NOMEM          = -1,
    SPL_ERR_INVALID_MAGIC  = -2,
    SPL_ERR_INVALID_CRC    = -3,
    SPL_ERR_INVALID_SEQ    = -4,
    SPL_ERR_INVALID_EPOCH  = -5,
    SPL_ERR_INVALID_FRAME  = -6,
    SPL_ERR_INVALID_ID     = -7,
    SPL_ERR_INVALID_STATE  = -8,
    SPL_ERR_INVALID_TOKEN  = -9,
    SPL_ERR_INVALID_LEASE  = -10,
    SPL_ERR_NOT_FOUND      = -11,
    SPL_ERR_FULL           = -12,
    SPL_ERR_EXPIRED        = -13,
    SPL_ERR_FROZEN         = -14,
    SPL_ERR_DUPLICATE      = -15,
    SPL_ERR_PERMISSION     = -16,
    SPL_ERR_OVERFLOW       = -17,
    SPL_ERR_UNDERFLOW      = -18,
    SPL_ERR_TRUNCATED      = -19,
    SPL_ERR_PROTOCOL       = -20,
    SPL_ERR_MIGRATION      = -21,
    SPL_ERR_REPLICATION    = -22,
    SPL_ERR_CHECKPOINT     = -23,
    SPL_ERR_RETENTION      = -24,
    SPL_ERR_ROUTE          = -25,
    SPL_ERR_BACKPRESSURE   = -26,
    SPL_ERR_BUSY           = -27,
    SPL_ERR_CLOSED         = -28
} spl_error_t;

/* ── Opaque handles ──────────────────────────────────────────────── */
typedef struct spl_context       spl_context_t;
typedef struct spl_session       spl_session_t;

/* ── Frame types ─────────────────────────────────────────────────── */
typedef enum {
    SPL_FRAME_SESSION_INIT       = 0x01,
    SPL_FRAME_SESSION_CAP_NEG    = 0x02,
    SPL_FRAME_SESSION_EPOCH_ADV  = 0x03,
    SPL_FRAME_SESSION_REKEY      = 0x04,
    SPL_FRAME_SESSION_CLOSE      = 0x05,

    SPL_FRAME_SPAN_CREATE        = 0x10,
    SPL_FRAME_SPAN_CLOSE         = 0x11,
    SPL_FRAME_SPAN_META_UPDATE   = 0x12,

    SPL_FRAME_SEGMENT_ATTACH     = 0x18,
    SPL_FRAME_SEGMENT_DETACH     = 0x19,
    SPL_FRAME_SEGMENT_COMPACT    = 0x1A,

    SPL_FRAME_CHANNEL_OPEN       = 0x20,
    SPL_FRAME_CHANNEL_CLOSE      = 0x21,
    SPL_FRAME_CHANNEL_BIND       = 0x22,
    SPL_FRAME_CHANNEL_REMAP      = 0x23,
    SPL_FRAME_CHANNEL_DRAIN      = 0x24,

    SPL_FRAME_LEASE_ACQUIRE      = 0x30,
    SPL_FRAME_LEASE_RELEASE      = 0x31,
    SPL_FRAME_LEASE_RENEW        = 0x32,

    SPL_FRAME_STREAM_OPEN        = 0x40,
    SPL_FRAME_STREAM_CLOSE       = 0x41,
    SPL_FRAME_STREAM_APPEND      = 0x42,
    SPL_FRAME_STREAM_READ        = 0x43,
    SPL_FRAME_STREAM_FLUSH       = 0x44,

    SPL_FRAME_CURSOR_OPEN        = 0x50,
    SPL_FRAME_CURSOR_CLOSE       = 0x51,
    SPL_FRAME_CURSOR_ADVANCE     = 0x52,
    SPL_FRAME_CURSOR_READ        = 0x53,

    SPL_FRAME_REPL_SETUP         = 0x60,
    SPL_FRAME_REPL_ADD_MEMBER    = 0x61,
    SPL_FRAME_REPL_REMOVE_MEMBER = 0x62,
    SPL_FRAME_REPL_SYNC_BEGIN    = 0x63,
    SPL_FRAME_REPL_SYNC_DATA     = 0x64,
    SPL_FRAME_REPL_CHECK         = 0x65,

    SPL_FRAME_MIGRATE_INITIATE   = 0x70,
    SPL_FRAME_MIGRATE_PREPARE    = 0x71,
    SPL_FRAME_MIGRATE_TRANSFER   = 0x72,
    SPL_FRAME_MIGRATE_HANDOFF    = 0x73,
    SPL_FRAME_MIGRATE_ABORT      = 0x74,

    SPL_FRAME_CHECKPOINT_BEGIN   = 0x80,
    SPL_FRAME_CHECKPOINT_FLUSH   = 0x81,
    SPL_FRAME_CHECKPOINT_RESTORE = 0x82,

    SPL_FRAME_RETENTION_EVAL     = 0x88,
    SPL_FRAME_RETENTION_COLLECT  = 0x89,

    SPL_FRAME_ROUTE_ADVERTISE    = 0x90,
    SPL_FRAME_ROUTE_WITHDRAW     = 0x91,
    SPL_FRAME_ROUTE_REBALANCE    = 0x92,
    SPL_FRAME_ROUTE_NODE_JOIN    = 0x93,
    SPL_FRAME_ROUTE_NODE_DEPART  = 0x94,

    SPL_FRAME_PARTITION_ADD      = 0xA0,
    SPL_FRAME_PARTITION_REMOVE   = 0xA1,

    SPL_FRAME_BP_CREDIT_UPDATE   = 0xB0,
    SPL_FRAME_BP_WINDOW_ADJUST   = 0xB1,

    SPL_FRAME_DRAIN_DEFERRED     = 0xC0,
    SPL_FRAME_GC_TRIGGER         = 0xC1,

    SPL_FRAME_PADDING            = 0xFF
} spl_frame_type_t;

/* ── Subrecord kinds ─────────────────────────────────────────────── */
typedef enum {
    SPL_SUB_DATA              = 0x01,
    SPL_SUB_META              = 0x02,
    SPL_SUB_OWNERSHIP_XFER    = 0x03,
    SPL_SUB_LEASE_GRANT       = 0x04,
    SPL_SUB_CURSOR_POS        = 0x05,
    SPL_SUB_SEGMENT_REF       = 0x06,
    SPL_SUB_ROUTE_ENTRY       = 0x07,
    SPL_SUB_REPLICA_DESC      = 0x08,
    SPL_SUB_CHECKPOINT_SNAP   = 0x09,
    SPL_SUB_CREDIT_GRANT      = 0x0A,
    SPL_SUB_WATERMARK         = 0x0B,
    SPL_SUB_CRYPTO_CTX        = 0x0C,
    SPL_SUB_PARTITION_DESC    = 0x0D,
    SPL_SUB_NODE_DESC         = 0x0E,
    SPL_SUB_MIGRATION_STATE   = 0x0F,
    SPL_SUB_GEN_STAMP         = 0x10
} spl_subrecord_kind_t;

/* ── Capability flags ────────────────────────────────────────────── */
#define SPL_CAP_CHANNEL       (1U << 0)
#define SPL_CAP_STREAM        (1U << 1)
#define SPL_CAP_CURSOR        (1U << 2)
#define SPL_CAP_REPLICATION   (1U << 3)
#define SPL_CAP_MIGRATION     (1U << 4)
#define SPL_CAP_CHECKPOINT    (1U << 5)
#define SPL_CAP_RETENTION     (1U << 6)
#define SPL_CAP_BACKPRESSURE  (1U << 7)
#define SPL_CAP_CRYPTO        (1U << 8)

/* ── Public API ──────────────────────────────────────────────────── */

/*
 * Context lifecycle
 */
spl_context_t *spl_context_create(void);
void           spl_context_destroy(spl_context_t *ctx);

/*
 * Session lifecycle
 */
spl_session_t *spl_session_create(spl_context_t *ctx, uint32_t capabilities);
void           spl_session_destroy(spl_session_t *sess);

/*
 * Primary frame ingestion entry point.
 * Parses and dispatches validated protocol frames.
 * Returns number of frames successfully processed, or negative error.
 */
int spl_ingest_frames(spl_session_t *sess, const uint8_t *data, size_t len);

/*
 * Query session state
 */
uint32_t spl_session_get_epoch(const spl_session_t *sess);
uint32_t spl_session_get_id(const spl_session_t *sess);
uint32_t spl_session_get_capabilities(const spl_session_t *sess);
int      spl_session_get_channel_count(const spl_session_t *sess);
int      spl_session_get_span_count(const spl_session_t *sess);

/*
 * Version info
 */
const char *spl_version_string(void);
uint32_t    spl_version_numeric(void);

#ifdef __cplusplus
}
#endif

#endif /* SPANLOG_H */

/*
 * Session management functions.
 *
 * A session represents a single client connection to the spanlog
 * coordination layer. Sessions are created with a set of requested
 * capabilities, then initialized and negotiated before protocol
 * frames can be processed.
 *
 * Session lifecycle:
 *   1. spl_session_create(ctx, caps) - allocate session with requested capabilities
 *   2. INIT frame - initialize session state, assign session ID
 *   3. CAP_NEG frame - negotiate capabilities with peer
 *   4. ... process frames ...
 *   5. spl_session_destroy(sess) - cleanup all resources
 *
 * Epoch management:
 *   Sessions track an epoch counter that monotonically increases.
 *   Epoch advances trigger:
 *     - Migration preparation for spans needing rebalancing
 *     - Garbage collection of drained channels
 *     - Invalidation of old-epoch leases
 *   Epoch is advanced by EPOCH_ADV frames with a requested epoch
 *   value in the first subrecord.
 *
 * Session capabilities are negotiated once and immutable after
 * negotiation. The intersection of local and remote capabilities
 * determines which frame types the session will accept.
 */

/*
 * Span management functions.
 *
 * A span is a logical unit of log storage identified by a span_id.
 * Spans contain ordered segments of data and are the primary
 * unit of migration, replication, and access control.
 *
 * Span lifecycle states:
 *   ACTIVE    - normal operation, accepting reads and writes
 *   DRAINING  - no new operations, existing complete normally
 *   CLOSED    - all operations complete, awaiting cleanup
 *   MIGRATING - data being transferred to another node
 *   DEFERRED  - marked for deletion after grace period
 *   FREED     - memory released
 *
 * Refcounting:
 *   Spans use reference counting for lifecycle management.
 *   References are held by: leases, cursors, checkpoints,
 *   replication snapshots, and migration operations.
 *   When refcount reaches zero, the span is freed.
 */

/*
 * Segment storage functions.
 *
 * Segments are the physical storage units within spans.
 * Each segment has a fixed capacity and variable used size.
 * Segments support append-only writes and random reads.
 *
 * Segment operations:
 *   alloc  - allocate a new segment with given capacity
 *   write  - append data to segment (fails if full)
 *   realloc - resize segment (may move data, invalidating pointers)
 *   compact - merge multiple segments into one
 *   gc     - reclaim freed segments
 */

/*
 * Lease management functions.
 *
 * Leases provide time-bounded access control for spans.
 * A lease grants read/write access to a specific span for a
 * duration measured in epoch ticks. Leases can be renewed
 * to extend their lifetime.
 *
 * Lease types:
 *   - Channel leases: acquired by channels for data operations
 *   - Checkpoint leases: acquired by checkpoint listeners
 *   - Migration leases: acquired during span migration
 *
 * When a lease expires or is force-released (epoch invalidation),
 * the span's refcount is decremented. If refcount reaches zero,
 * the span is freed.
 */

/*
 * Cursor functions.
 *
 * Cursors provide read access to span data through cached
 * segment pointers. A cursor is positioned at a specific
 * segment and offset within a span.
 *
 * Cursor operations:
 *   open      - create cursor on a span (requires active lease)
 *   advance   - move to a specific segment (caches data pointer)
 *   materialize - read data from current position
 *   invalidate - clear cached pointers (after segment changes)
 *   close     - release cursor
 */
