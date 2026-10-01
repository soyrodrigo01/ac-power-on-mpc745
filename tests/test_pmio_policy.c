#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../pmio_policy.h"

struct model {
    uint8_t value;
    unsigned reads;
    unsigned writes;
    uint8_t written;
    int ignore_write;
    int flap_on_third_read;
};

static uint8_t model_read(void *ctx) {
    struct model *m = ctx;
    m->reads++;
    if (m->flap_on_third_read && m->reads == 3U) return 0x44;
    return m->value;
}

static void model_write(void *ctx, uint8_t value) {
    struct model *m = ctx;
    m->writes++;
    m->written = value;
    if (m->ignore_write) return;
    /* PMIO 0x5B upper nibble is a read-only mirror of the lower nibble. */
    const uint8_t low = value & 0x0fU;
    m->value = (uint8_t)(low | (uint8_t)(low << 4));
}

static void model_fence(void *ctx) { (void)ctx; }
static void model_delay(void *ctx) { (void)ctx; }

static void test_off_to_on_writes_once(void) {
    struct model model = {.value = 0x44};
    const struct pmio_ops ops = {
        .read8 = model_read,
        .write8 = model_write,
        .fence = model_fence,
        .delay = model_delay,
        .ctx = &model,
    };
    struct pmio_observation obs = {0};

    const enum pmio_result result = pmio_apply_always_on(&ops, &obs);

    assert(result == PMIO_OK_WROTE);
    assert(obs.before == 0x44);
    assert(obs.written == 0x45);
    assert(obs.after_first == 0x55);
    assert(obs.after_second == 0x55);
    assert(model.writes == 1);
    assert(model.written == 0x45);
}

static void test_already_on_writes_nothing(void) {
    struct model model = {.value = 0x55};
    const struct pmio_ops ops = {
        .read8 = model_read,
        .write8 = model_write,
        .fence = model_fence,
        .delay = model_delay,
        .ctx = &model,
    };
    struct pmio_observation obs = {0};

    const enum pmio_result result = pmio_apply_always_on(&ops, &obs);

    assert(result == PMIO_OK_ALREADY_ON);
    assert(obs.before == 0x55);
    assert(model.writes == 0);
}

static struct pmio_ops make_ops(struct model *model) {
    return (struct pmio_ops){
        .read8 = model_read,
        .write8 = model_write,
        .fence = model_fence,
        .delay = model_delay,
        .ctx = model,
    };
}

static void test_unexpected_states_refuse_without_writing(void) {
    const uint8_t states[] = {0x00, 0x45, 0x46, 0x47, 0x54, 0x64, 0xff};
    for (size_t i = 0; i < sizeof(states); i++) {
        struct model model = {.value = states[i]};
        const struct pmio_ops ops = make_ops(&model);
        struct pmio_observation obs = {0};
        assert(pmio_apply_always_on(&ops, &obs) == PMIO_REFUSE_STATE);
        assert(model.writes == 0);
    }
}

static void test_lost_write_is_not_retried(void) {
    struct model model = {.value = 0x44, .ignore_write = 1};
    const struct pmio_ops ops = make_ops(&model);
    struct pmio_observation obs = {0};
    assert(pmio_apply_always_on(&ops, &obs) == PMIO_REFUSE_READBACK);
    assert(model.writes == 1);
    assert(obs.after_first == 0x44);
}

static void test_unstable_readback_is_not_retried(void) {
    struct model model = {.value = 0x44, .flap_on_third_read = 1};
    const struct pmio_ops ops = make_ops(&model);
    struct pmio_observation obs = {0};
    assert(pmio_apply_always_on(&ops, &obs) == PMIO_REFUSE_UNSTABLE);
    assert(model.writes == 1);
    assert(obs.after_first == 0x55);
    assert(obs.after_second == 0x44);
}

int main(void) {
    test_off_to_on_writes_once();
    test_already_on_writes_nothing();
    test_unexpected_states_refuse_without_writing();
    test_lost_write_is_not_retried();
    test_unstable_readback_is_not_retried();
    puts("test_pmio_policy: PASS");
    return 0;
}
