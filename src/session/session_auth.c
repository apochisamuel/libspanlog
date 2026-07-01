#include "../common/types.h"
#include "../common/alloc.h"

/*
 * Session crypto context rotation.
 * Reallocates the crypto context buffer for key rotation.
 */
int spl_session_rotate_context(spl_session_t *sess, const spl_parsed_frame_t *f) {
    if (!sess || !f) return SPL_ERR_INVALID_FRAME;
    if (!sess->initialized || !sess->negotiated)
        return SPL_ERR_INVALID_STATE;
    if (!(sess->capabilities & SPL_CAP_CRYPTO))
        return SPL_ERR_INVALID_STATE;

    /* Validate rekey subrecord */
    if (f->parsed_subrecord_count < 1) return SPL_ERR_INVALID_FRAME;
    const spl_subrecord_t *sub = &f->subrecords[0];
    if (sub->kind != SPL_SUB_CRYPTO_CTX) return SPL_ERR_INVALID_FRAME;
    if (sub->generation != sess->crypto_generation)
        return SPL_ERR_INVALID_STATE;

    /* Free old crypto context */
    spl_free(sess->crypto_ctx);

    /* Allocate new context with updated key material */
    size_t new_len = sess->crypto_ctx_len;
    if (sub->payload_len >= 2) {
        uint16_t req_len = spl_read_u16(sub->payload);
        if (req_len >= 32 && req_len <= 256)
            new_len = req_len;
    }

    sess->crypto_ctx = spl_alloc(new_len);
    if (!sess->crypto_ctx) {
        sess->crypto_ctx_len = 0;
        return SPL_ERR_NOMEM;
    }
    sess->crypto_ctx_len = new_len;
    sess->crypto_generation++;

    /* Derive new key material from session state */
    uint32_t seed = spl_hash_mix(sess->session_id, sess->crypto_generation);
    for (size_t i = 0; i < new_len; i++) {
        seed = spl_hash_mix(seed, (uint32_t)i);
        sess->crypto_ctx[i] = (uint8_t)(seed & 0xFF);
    }

    return SPL_OK;
}
