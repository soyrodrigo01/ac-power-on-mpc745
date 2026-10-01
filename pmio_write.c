#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "pmio_identity.h"
#include "pmio_map.h"
#include "pmio_policy.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TARGET_PHYS UINT64_C(0xFED8035B)
#define PAGE_BASE   UINT64_C(0xFED80000)
#define PAGE_OFFSET ((size_t)(TARGET_PHYS - PAGE_BASE))
#define DWORD_BASE_OFFSET ((size_t)0x358)

_Static_assert(PAGE_OFFSET == 0x35bU, "unexpected PMIO page offset");
_Static_assert(PAGE_OFFSET == DWORD_BASE_OFFSET + 3U, "target must be byte 3 of PMIO dword");
#if !defined(__x86_64__)
#error "This helper is restricted to the verified x86_64 platform"
#endif

struct mmio_context {
    volatile uint8_t *reg;
};

static uint8_t mmio_read8(void *opaque) {
    const struct mmio_context *ctx = opaque;
    return *ctx->reg;
}

static void mmio_write8(void *opaque, uint8_t value) {
    const struct mmio_context *ctx = opaque;
    *ctx->reg = value;
}

static void mmio_fence(void *opaque) {
    (void)opaque;
    atomic_thread_fence(memory_order_seq_cst);
}

static void mmio_delay(void *opaque) {
    (void)opaque;
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000L};
    (void)nanosleep(&delay, NULL);
}

static const char *result_name(enum pmio_result result) {
    switch (result) {
        case PMIO_OK_WROTE: return "OK_WROTE";
        case PMIO_OK_ALREADY_ON: return "OK_ALREADY_ON";
        case PMIO_REFUSE_STATE: return "REFUSE_STATE";
        case PMIO_REFUSE_READBACK: return "REFUSE_READBACK";
        case PMIO_REFUSE_UNSTABLE: return "REFUSE_UNSTABLE";
    }
    return "UNKNOWN";
}

static int result_exit_code(enum pmio_result result) {
    switch (result) {
        case PMIO_OK_WROTE:
        case PMIO_OK_ALREADY_ON:
            return 0;
        case PMIO_REFUSE_STATE:
            return 10;
        case PMIO_REFUSE_READBACK:
            return 11;
        case PMIO_REFUSE_UNSTABLE:
            return 12;
    }
    return 19;
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "REFUSED: this helper accepts no arguments\n");
        return 14;
    }
    if (geteuid() != 0) {
        fprintf(stderr, "REFUSED: root required for /dev/mem\n");
        return 13;
    }
    if (!pmio_identity_matches("/sys/class/dmi/id")) {
        fprintf(stderr, "REFUSED: board/BIOS identity mismatch\n");
        return 15;
    }

    struct pmio_mapping map = {0};
    const int map_error = pmio_map_open("/dev/mem", (off_t)PAGE_BASE, 1, &map);
    if (map_error != 0) {
        fprintf(stderr, "REFUSED: /dev/mem map failed: %s\n", strerror(map_error));
        return 16;
    }

    uint8_t dword_before[4];
    uint8_t dword_after[4];
    for (size_t i = 0; i < 4U; i++) dword_before[i] = map.base[DWORD_BASE_OFFSET + i];

    struct mmio_context context = {.reg = map.base + PAGE_OFFSET};
    const struct pmio_ops ops = {
        .read8 = mmio_read8,
        .write8 = mmio_write8,
        .fence = mmio_fence,
        .delay = mmio_delay,
        .ctx = &context,
    };
    struct pmio_observation obs = {0};
    const enum pmio_result result = pmio_apply_always_on(&ops, &obs);

    atomic_thread_fence(memory_order_seq_cst);
    for (size_t i = 0; i < 4U; i++) dword_after[i] = map.base[DWORD_BASE_OFFSET + i];
    const int close_error = pmio_map_close(&map);
    if (close_error != 0) {
        fprintf(stderr, "ERROR: unmap/close failed: %s\n", strerror(close_error));
        return 17;
    }

    int neighbours_unchanged = 1;
    for (size_t i = 0; i < 3U; i++) {
        if (dword_before[i] != dword_after[i]) neighbours_unchanged = 0;
    }

    printf("pmio_ac_loss addr=0x%08llX before=0x%02X write=0x%02X "
           "after1=0x%02X after2=0x%02X result=%s\n",
           (unsigned long long)TARGET_PHYS, (unsigned int)obs.before,
           (unsigned int)obs.written, (unsigned int)obs.after_first,
           (unsigned int)obs.after_second, result_name(result));
    printf("neighbour_dword before=%02X%02X%02X%02X after=%02X%02X%02X%02X\n",
           dword_before[3], dword_before[2], dword_before[1], dword_before[0],
           dword_after[3], dword_after[2], dword_after[1], dword_after[0]);

    if (!neighbours_unchanged) {
        fprintf(stderr, "ERROR: collateral PMIO byte change detected; no retry\n");
        return 18;
    }
    return result_exit_code(result);
}
