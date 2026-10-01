#include "pmio_policy.h"

#include <stddef.h>

#define PMIO_OFF_VALUE 0x44U
#define PMIO_WRITE_ALWAYS_ON 0x45U
#define PMIO_READ_ALWAYS_ON 0x55U

enum pmio_result pmio_apply_always_on(const struct pmio_ops *ops,
                                      struct pmio_observation *obs) {
    if (ops == NULL || obs == NULL || ops->read8 == NULL || ops->write8 == NULL ||
        ops->fence == NULL || ops->delay == NULL) {
        return PMIO_REFUSE_STATE;
    }

    obs->before = ops->read8(ops->ctx);
    if (obs->before == PMIO_READ_ALWAYS_ON) {
        return PMIO_OK_ALREADY_ON;
    }
    if (obs->before != PMIO_OFF_VALUE) {
        return PMIO_REFUSE_STATE;
    }

    obs->written = (uint8_t)((obs->before & 0xf0U) | 0x05U);
    if (obs->written != PMIO_WRITE_ALWAYS_ON) {
        return PMIO_REFUSE_STATE;
    }

    ops->fence(ops->ctx);
    ops->write8(ops->ctx, obs->written);
    ops->fence(ops->ctx);
    obs->after_first = ops->read8(ops->ctx);
    if (obs->after_first != PMIO_READ_ALWAYS_ON) {
        return PMIO_REFUSE_READBACK;
    }

    ops->fence(ops->ctx);
    ops->delay(ops->ctx);
    obs->after_second = ops->read8(ops->ctx);
    if (obs->after_second != PMIO_READ_ALWAYS_ON) {
        return PMIO_REFUSE_UNSTABLE;
    }

    return PMIO_OK_WROTE;
}
