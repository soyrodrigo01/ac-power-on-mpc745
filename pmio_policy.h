#ifndef PMIO_POLICY_H
#define PMIO_POLICY_H

#include <stdint.h>

enum pmio_result {
    PMIO_OK_WROTE = 0,
    PMIO_OK_ALREADY_ON,
    PMIO_REFUSE_STATE,
    PMIO_REFUSE_READBACK,
    PMIO_REFUSE_UNSTABLE,
};

struct pmio_ops {
    uint8_t (*read8)(void *ctx);
    void (*write8)(void *ctx, uint8_t value);
    void (*fence)(void *ctx);
    void (*delay)(void *ctx);
    void *ctx;
};

struct pmio_observation {
    uint8_t before;
    uint8_t written;
    uint8_t after_first;
    uint8_t after_second;
};

enum pmio_result pmio_apply_always_on(const struct pmio_ops *ops,
                                      struct pmio_observation *obs);

#endif
