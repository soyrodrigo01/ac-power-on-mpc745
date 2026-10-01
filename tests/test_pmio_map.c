#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../pmio_map.h"

static void test_shared_page_mapping_changes_only_target_byte(void) {
    char path[] = "/tmp/pmio-map-test-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);

    const long page_size = sysconf(_SC_PAGESIZE);
    assert(page_size == 4096);
    uint8_t *original = malloc((size_t)page_size);
    uint8_t *after = malloc((size_t)page_size);
    assert(original != NULL && after != NULL);
    memset(original, 0xa5, (size_t)page_size);
    original[0x35b] = 0x44;
    assert(write(fd, original, (size_t)page_size) == page_size);
    assert(close(fd) == 0);

    struct pmio_mapping map = {0};
    assert(pmio_map_open(path, 0, 1, &map) == 0);
    assert(map.length == (size_t)page_size);
    assert(map.base != NULL);
    map.base[0x35b] = 0x45;
    assert(pmio_map_close(&map) == 0);

    const int rd = open(path, O_RDONLY | O_CLOEXEC);
    assert(rd >= 0);
    assert(read(rd, after, (size_t)page_size) == page_size);
    assert(close(rd) == 0);
    assert(after[0x35b] == 0x45);
    for (long i = 0; i < page_size; i++) {
        if (i != 0x35b) assert(after[i] == original[i]);
    }

    assert(unlink(path) == 0);
    free(after);
    free(original);
}

int main(void) {
    test_shared_page_mapping_changes_only_target_byte();
    puts("test_pmio_map: PASS");
    return 0;
}
