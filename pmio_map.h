#ifndef PMIO_MAP_H
#define PMIO_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct pmio_mapping {
    volatile uint8_t *base;
    size_t length;
    int fd;
};

int pmio_map_open(const char *path, off_t page_base, int writable,
                  struct pmio_mapping *mapping);
int pmio_map_close(struct pmio_mapping *mapping);

#endif
