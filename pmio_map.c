#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "pmio_map.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

int pmio_map_open(const char *path, off_t page_base, int writable,
                  struct pmio_mapping *mapping) {
    if (path == NULL || mapping == NULL) return EINVAL;
    mapping->base = NULL;
    mapping->length = 0;
    mapping->fd = -1;

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0 || ((uint64_t)page_size & ((uint64_t)page_size - 1U)) != 0U)
        return EINVAL;
    if (((uint64_t)page_base & ((uint64_t)page_size - 1U)) != 0U)
        return EINVAL;

    const int flags = writable ? (O_RDWR | O_SYNC | O_CLOEXEC) : (O_RDONLY | O_CLOEXEC);
    const int fd = open(path, flags);
    if (fd < 0) return errno;

    const int prot = writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
    void *const base = mmap(NULL, (size_t)page_size, prot, MAP_SHARED, fd, page_base);
    if (base == MAP_FAILED) {
        const int saved = errno;
        close(fd);
        return saved;
    }

    mapping->base = base;
    mapping->length = (size_t)page_size;
    mapping->fd = fd;
    return 0;
}

int pmio_map_close(struct pmio_mapping *mapping) {
    if (mapping == NULL || mapping->base == NULL || mapping->length == 0 || mapping->fd < 0)
        return EINVAL;

    int result = 0;
    if (munmap((void *)mapping->base, mapping->length) != 0) result = errno;
    if (close(mapping->fd) != 0 && result == 0) result = errno;
    mapping->base = NULL;
    mapping->length = 0;
    mapping->fd = -1;
    return result;
}
