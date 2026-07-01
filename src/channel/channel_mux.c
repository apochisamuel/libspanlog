#include "../common/types.h"
#include "../common/alloc.h"

spl_channel_t *spl_channel_mux_alloc(spl_session_t *sess, uint32_t channel_id) {
    if (!sess) return NULL;
    if (sess->channel_count >= SPL_MAX_CHANNELS) return NULL;
    if (!(sess->capabilities & SPL_CAP_CHANNEL)) return NULL;

    /* Check for duplicate */
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i] && sess->channels[i]->channel_id == channel_id)
            return NULL;
    }

    spl_channel_t *ch = spl_calloc(1, sizeof(spl_channel_t));
    if (!ch) return NULL;

    ch->channel_id = channel_id;
    ch->session_id = sess->session_id;
    ch->state = SPL_LIFECYCLE_CREATED;

    sess->channels[sess->channel_count++] = ch;
    return ch;
}

int spl_channel_mux_free(spl_session_t *sess, spl_channel_t *ch) {
    if (!sess || !ch) return SPL_ERR_INVALID_STATE;

    /* Remove from session registry */
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i] == ch) {
            sess->channels[i] = NULL;
            break;
        }
    }

    spl_free(ch);
    return SPL_OK;
}

spl_channel_t *spl_channel_lookup(spl_session_t *sess, uint32_t channel_id) {
    if (!sess) return NULL;
    for (uint16_t i = 0; i < sess->channel_count; i++) {
        if (sess->channels[i] && sess->channels[i]->channel_id == channel_id)
            return sess->channels[i];
    }
    return NULL;
}
