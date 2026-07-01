#include "error.h"
const char *spl_error_string(spl_error_t err) {
    switch (err) {
    case SPL_OK:               return "success";
    case SPL_ERR_NOMEM:        return "out of memory";
    case SPL_ERR_INVALID_MAGIC: return "invalid magic";
    case SPL_ERR_INVALID_CRC:  return "CRC mismatch";
    case SPL_ERR_INVALID_SEQ:  return "sequence mismatch";
    case SPL_ERR_INVALID_EPOCH: return "epoch mismatch";
    case SPL_ERR_INVALID_FRAME: return "invalid frame";
    case SPL_ERR_INVALID_ID:   return "invalid identifier";
    case SPL_ERR_INVALID_STATE: return "invalid state";
    case SPL_ERR_INVALID_TOKEN: return "invalid token";
    case SPL_ERR_INVALID_LEASE: return "invalid lease";
    case SPL_ERR_NOT_FOUND:    return "not found";
    case SPL_ERR_FULL:         return "registry full";
    case SPL_ERR_EXPIRED:      return "expired";
    case SPL_ERR_FROZEN:       return "frozen";
    case SPL_ERR_DUPLICATE:    return "duplicate";
    case SPL_ERR_PERMISSION:   return "permission denied";
    case SPL_ERR_OVERFLOW:     return "overflow";
    case SPL_ERR_UNDERFLOW:    return "underflow";
    case SPL_ERR_TRUNCATED:    return "truncated";
    case SPL_ERR_PROTOCOL:     return "protocol error";
    case SPL_ERR_MIGRATION:    return "migration error";
    case SPL_ERR_REPLICATION:  return "replication error";
    case SPL_ERR_CHECKPOINT:   return "checkpoint error";
    case SPL_ERR_RETENTION:    return "retention error";
    case SPL_ERR_ROUTE:        return "route error";
    case SPL_ERR_BACKPRESSURE: return "backpressure error";
    case SPL_ERR_BUSY:         return "busy";
    case SPL_ERR_CLOSED:       return "closed";
    default:                   return "unknown error";
    }
}
