/*
 * libspanlog internal declarations.
 * Not part of the public API — used by subsystem implementations.
 */
#ifndef SPL_INTERNAL_H
#define SPL_INTERNAL_H

#include "spanlog.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ═══════ Wire Format Constants ═══════ */

/*
 * Session Header Layout (20 bytes):
 *   [0..3]   magic      u32  0x53504C31 ("SPL1")
 *   [4..5]   version    u16  major.minor (0x0100 = 1.0)
 *   [6..9]   session_id u32
 *   [10..13] epoch      u32
 *   [14..15] frame_count u16
 *   [16..19] header_crc u32  CRC-32C over bytes [0..13]
 *
 * Frame Layout (20 bytes header + variable payload):
 *   [0..1]   total_length  u16  (includes header + payload + CRC)
 *   [2]      frame_type    u8
 *   [3..4]   sequence      u16
 *   [5..8]   session_id    u32
 *   [9..10]  channel_id    u16
 *   [11..12] route_id      u16
 *   [13..16] owner_token   u32
 *   [17..18] flags         u16
 *   [19]     sub_count     u8
 *   [20..]   subrecords[]  variable
 *   [N..N+3] payload_crc   u32  CRC-32C over [20..N-1]
 *
 * Subrecord Layout (15 bytes header + variable payload):
 *   [0]      kind          u8
 *   [1..2]   payload_len   u16
 *   [3..6]   local_epoch   u32
 *   [7..10]  generation    u32
 *   [11..14] lease_token   u32
 *   [15..]   payload[]     variable (payload_len bytes)
 */

/* ═══════ Frame Type Registry ═══════ */
/* Session management: 0x01-0x0F */
/* Span operations:    0x10-0x1F */
/* Segment operations: 0x20-0x2F */
/* Lease operations:   0x30-0x3F */
/* Channel operations: 0x40-0x4F */
/* Stream operations:  0x50-0x5F */
/* Cursor operations:  0x60-0x6F */
/* Replication:        0x70-0x7F */
/* Migration:          0x80-0x8F */
/* Checkpoint:         0x90-0x9F */
/* Retention:          0xA0-0xAF */
/* Route:              0xB0-0xBF */
/* Backpressure:       0xC0-0xCF */
/* Partition:          0xD0-0xDF */

/* ═══════ Subrecord Kind Registry ═══════ */
/* SPL_SUB_DATA           = 0x01  Raw data payload */
/* SPL_SUB_META           = 0x02  Metadata key-value */
/* SPL_SUB_CURSOR_POS     = 0x03  Cursor position */
/* SPL_SUB_LEASE_GRANT    = 0x04  Lease grant/revoke */
/* SPL_SUB_GEN_STAMP      = 0x05  Generation stamp */
/* SPL_SUB_SEGMENT_REF    = 0x06  Segment reference */
/* SPL_SUB_ROUTE_ENTRY    = 0x07  Route table entry */
/* SPL_SUB_CREDIT_GRANT   = 0x08  Credit allocation */
/* SPL_SUB_REPLICA_DESC   = 0x09  Replica descriptor */
/* SPL_SUB_MIGRATION_STATE = 0x0A Migration state */
/* SPL_SUB_NODE_DESC      = 0x0B  Node descriptor */
/* SPL_SUB_PARTITION_DESC = 0x0C  Partition descriptor */
/* SPL_SUB_CRYPTO_CTX     = 0x0D  Crypto context */

/* ═══════ Capability Bits ═══════ */
/* SPL_CAP_CHANNEL      = 0x0001 */
/* SPL_CAP_STREAM       = 0x0002 */
/* SPL_CAP_CURSOR       = 0x0004 */
/* SPL_CAP_REPLICATION  = 0x0008 */
/* SPL_CAP_MIGRATION    = 0x0010 */
/* SPL_CAP_CHECKPOINT   = 0x0020 */
/* SPL_CAP_RETENTION    = 0x0040 */
/* SPL_CAP_BACKPRESSURE = 0x0080 */
/* SPL_CAP_CRYPTO       = 0x0100 */

/* ═══════ Error Codes ═══════ */
/* SPL_OK               =  0 */
/* SPL_ERR_NOMEM        = -1 */
/* SPL_ERR_INVALID_MAGIC = -2 */
/* SPL_ERR_INVALID_CRC  = -3 */
/* SPL_ERR_INVALID_FRAME = -4 */
/* SPL_ERR_INVALID_STATE = -5 */
/* SPL_ERR_INVALID_ID   = -6 */
/* SPL_ERR_INVALID_SEQ  = -7 */
/* SPL_ERR_NOT_FOUND    = -8 */
/* SPL_ERR_FULL         = -9 */
/* SPL_ERR_OVERFLOW     = -10 */
/* SPL_ERR_LEASE_EXPIRED = -11 */
/* SPL_ERR_REPLICATION  = -12 */

/* ═══════ Lifecycle States ═══════ */
/* SPL_LIFECYCLE_ACTIVE    = 0 */
/* SPL_LIFECYCLE_DRAINING  = 1 */
/* SPL_LIFECYCLE_CLOSED    = 2 */
/* SPL_LIFECYCLE_MIGRATING = 3 */
/* SPL_LIFECYCLE_DEFERRED  = 4 */
/* SPL_LIFECYCLE_FREED     = 5 */

/* ═══════ Limits ═══════ */
/* SPL_MAX_SPANS       = 128 */
/* SPL_MAX_SEGMENTS    = 512 */
/* SPL_MAX_CHANNELS    = 64  */
/* SPL_MAX_STREAMS     = 256 */
/* SPL_MAX_CURSORS     = 128 */
/* SPL_MAX_LEASES      = 128 */
/* SPL_MAX_SUBRECORDS  = 8   */
/* SPL_MAX_REPLICAS    = 16  */
/* SPL_MAX_MIGRATIONS  = 16  */
/* SPL_MAX_CHECKPOINTS = 32  */
/* SPL_MAX_ROUTES      = 256 */
/* SPL_MAX_NODES       = 32  */
/* SPL_MAX_PARTITIONS  = 64  */
/* SPL_MAX_SEGMENT_SIZE = 16MB */

#endif /* SPL_INTERNAL_H */
