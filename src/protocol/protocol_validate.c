#include "../common/types.h"
#include "../common/crc.h"

/*
 * Deep frame validation pipeline.
 * Checks structural, semantic, and policy constraints beyond
 * basic CRC and format validation.
 */

typedef enum {
    SPL_VAL_OK            = 0,
    SPL_VAL_BAD_TYPE      = 1,
    SPL_VAL_BAD_SUBRECORD = 2,
    SPL_VAL_BAD_ORDERING  = 3,
    SPL_VAL_BAD_SIZE      = 4,
    SPL_VAL_BAD_SEMANTIC  = 5,
    SPL_VAL_POLICY_DENY   = 6
} spl_validation_code_t;

typedef struct {
    spl_validation_code_t code;
    uint8_t               frame_type;
    uint8_t               subrecord_idx;
    const char            *message;
} spl_validation_result_t;

/*
 * Validate frame type ordering: certain frame types must precede others.
 * E.g., SESSION_INIT must come before any other frame type.
 */
int spl_validate_ordering(const spl_session_t *sess,
                          const spl_parsed_frame_t *f,
                          spl_validation_result_t *result) {
    if (!sess || !f || !result) return -1;

    result->code = SPL_VAL_OK;
    result->frame_type = f->frame_type;

    /* Session must be initialized before any non-init frame */
    if (f->frame_type != SPL_FRAME_SESSION_INIT && !sess->initialized) {
        result->code = SPL_VAL_BAD_ORDERING;
        result->message = "session not initialized";
        return -1;
    }

    /* Capabilities must be negotiated before subsystem frames */
    if (!sess->negotiated) {
        switch (f->frame_type) {
        case SPL_FRAME_SESSION_INIT:
        case SPL_FRAME_SESSION_CAP_NEG:
        case SPL_FRAME_SESSION_CLOSE:
            break;  /* allowed before negotiation */
        default:
            result->code = SPL_VAL_BAD_ORDERING;
            result->message = "capabilities not negotiated";
            return -1;
        }
    }

    /* Cap-neg must come exactly once, after init */
    if (f->frame_type == SPL_FRAME_SESSION_CAP_NEG && sess->negotiated) {
        result->code = SPL_VAL_BAD_ORDERING;
        result->message = "duplicate capability negotiation";
        return -1;
    }

    return 0;
}

/*
 * Validate subrecord type compatibility with frame type.
 */
int spl_validate_subrecord_types(const spl_parsed_frame_t *f,
                                 spl_validation_result_t *result) {
    if (!f || !result) return -1;
    result->code = SPL_VAL_OK;

    for (uint8_t i = 0; i < f->parsed_subrecord_count; i++) {
        const spl_subrecord_t *sub = &f->subrecords[i];
        bool valid = true;

        switch (f->frame_type) {
        case SPL_FRAME_SPAN_CREATE:
        case SPL_FRAME_SPAN_META_UPDATE:
            valid = (sub->kind == SPL_SUB_META ||
                     sub->kind == SPL_SUB_GEN_STAMP);
            break;
        case SPL_FRAME_SEGMENT_ATTACH:
        case SPL_FRAME_SEGMENT_DETACH:
            valid = (sub->kind == SPL_SUB_SEGMENT_REF ||
                     sub->kind == SPL_SUB_DATA);
            break;
        case SPL_FRAME_LEASE_ACQUIRE:
        case SPL_FRAME_LEASE_RELEASE:
        case SPL_FRAME_LEASE_RENEW:
            valid = (sub->kind == SPL_SUB_LEASE_GRANT);
            break;
        case SPL_FRAME_CURSOR_OPEN:
        case SPL_FRAME_CURSOR_ADVANCE:
            valid = (sub->kind == SPL_SUB_CURSOR_POS);
            break;
        case SPL_FRAME_STREAM_APPEND:
            valid = (sub->kind == SPL_SUB_DATA);
            break;
        case SPL_FRAME_REPL_ADD_MEMBER:
        case SPL_FRAME_REPL_REMOVE_MEMBER:
            valid = (sub->kind == SPL_SUB_REPLICA_DESC);
            break;
        case SPL_FRAME_MIGRATE_INITIATE:
            valid = (sub->kind == SPL_SUB_MIGRATION_STATE);
            break;
        case SPL_FRAME_ROUTE_ADVERTISE:
            valid = (sub->kind == SPL_SUB_ROUTE_ENTRY);
            break;
        case SPL_FRAME_ROUTE_NODE_JOIN:
        case SPL_FRAME_ROUTE_NODE_DEPART:
            valid = (sub->kind == SPL_SUB_NODE_DESC);
            break;
        case SPL_FRAME_PARTITION_ADD:
        case SPL_FRAME_PARTITION_REMOVE:
        case SPL_FRAME_CHANNEL_BIND:
        case SPL_FRAME_CHANNEL_REMAP:
            valid = (sub->kind == SPL_SUB_PARTITION_DESC);
            break;
        case SPL_FRAME_BP_CREDIT_UPDATE:
        case SPL_FRAME_BP_WINDOW_ADJUST:
            valid = (sub->kind == SPL_SUB_CREDIT_GRANT);
            break;
        case SPL_FRAME_SESSION_REKEY:
            valid = (sub->kind == SPL_SUB_CRYPTO_CTX);
            break;
        default:
            valid = true;  /* unknown frame type: accept any subrecord */
        }

        if (!valid) {
            result->code = SPL_VAL_BAD_SUBRECORD;
            result->subrecord_idx = i;
            result->frame_type = f->frame_type;
            result->message = "incompatible subrecord kind for frame type";
            return -1;
        }
    }

    return 0;
}

/*
 * Validate capability requirements: check that the session has
 * the required capabilities for the given frame type.
 */
int spl_validate_capabilities(const spl_session_t *sess,
                              const spl_parsed_frame_t *f,
                              spl_validation_result_t *result) {
    if (!sess || !f || !result) return -1;
    result->code = SPL_VAL_OK;

    uint32_t required = 0;

    switch (f->frame_type) {
    case SPL_FRAME_CHANNEL_OPEN:
    case SPL_FRAME_CHANNEL_CLOSE:
    case SPL_FRAME_CHANNEL_BIND:
    case SPL_FRAME_CHANNEL_REMAP:
    case SPL_FRAME_CHANNEL_DRAIN:
        required = SPL_CAP_CHANNEL;
        break;
    case SPL_FRAME_STREAM_OPEN:
    case SPL_FRAME_STREAM_CLOSE:
    case SPL_FRAME_STREAM_APPEND:
    case SPL_FRAME_STREAM_READ:
    case SPL_FRAME_STREAM_FLUSH:
        required = SPL_CAP_STREAM;
        break;
    case SPL_FRAME_CURSOR_OPEN:
    case SPL_FRAME_CURSOR_CLOSE:
    case SPL_FRAME_CURSOR_ADVANCE:
    case SPL_FRAME_CURSOR_READ:
        required = SPL_CAP_CURSOR;
        break;
    case SPL_FRAME_REPL_SETUP:
    case SPL_FRAME_REPL_ADD_MEMBER:
    case SPL_FRAME_REPL_REMOVE_MEMBER:
    case SPL_FRAME_REPL_SYNC_BEGIN:
    case SPL_FRAME_REPL_SYNC_DATA:
    case SPL_FRAME_REPL_CHECK:
        required = SPL_CAP_REPLICATION;
        break;
    case SPL_FRAME_MIGRATE_INITIATE:
    case SPL_FRAME_MIGRATE_PREPARE:
    case SPL_FRAME_MIGRATE_TRANSFER:
    case SPL_FRAME_MIGRATE_HANDOFF:
    case SPL_FRAME_MIGRATE_ABORT:
        required = SPL_CAP_MIGRATION;
        break;
    case SPL_FRAME_CHECKPOINT_BEGIN:
    case SPL_FRAME_CHECKPOINT_FLUSH:
    case SPL_FRAME_CHECKPOINT_RESTORE:
        required = SPL_CAP_CHECKPOINT;
        break;
    case SPL_FRAME_RETENTION_EVAL:
    case SPL_FRAME_RETENTION_COLLECT:
        required = SPL_CAP_RETENTION;
        break;
    case SPL_FRAME_BP_CREDIT_UPDATE:
    case SPL_FRAME_BP_WINDOW_ADJUST:
        required = SPL_CAP_BACKPRESSURE;
        break;
    case SPL_FRAME_SESSION_REKEY:
        required = SPL_CAP_CRYPTO;
        break;
    default:
        required = 0;
    }

    if (required != 0 && !(sess->capabilities & required)) {
        result->code = SPL_VAL_POLICY_DENY;
        result->frame_type = f->frame_type;
        result->message = "missing required capability";
        return -1;
    }

    return 0;
}

/*
 * Full validation pipeline: runs all checks in order.
 * Returns 0 on success, -1 on first validation failure.
 */
int spl_validate_frame_full(const spl_session_t *sess,
                            const spl_parsed_frame_t *f,
                            spl_validation_result_t *result) {
    if (!sess || !f || !result) return -1;

    int rc;

    rc = spl_validate_ordering(sess, f, result);
    if (rc != 0) return rc;

    rc = spl_validate_capabilities(sess, f, result);
    if (rc != 0) return rc;

    rc = spl_validate_subrecord_types(f, result);
    if (rc != 0) return rc;

    return 0;
}
