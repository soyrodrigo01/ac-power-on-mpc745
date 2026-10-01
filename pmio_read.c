#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define TARGET_PHYS UINT64_C(0xFED8035B)

int main(void) {
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
        return 2;
    }

    const uint64_t page_mask = (uint64_t)page_size - 1U;
    const off_t page_base = (off_t)(TARGET_PHYS & ~page_mask);
    const size_t page_offset = (size_t)(TARGET_PHYS & page_mask);

    const int fd = open("/dev/mem", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "open /dev/mem: %s\n", strerror(errno));
        return 3;
    }

    const void *mapping = mmap(NULL, (size_t)page_size, PROT_READ, MAP_SHARED, fd, page_base);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "mmap 0x%llx: %s\n", (unsigned long long)page_base, strerror(errno));
        close(fd);
        return 4;
    }

    const volatile uint8_t *reg = (const volatile uint8_t *)mapping + page_offset;
    const uint8_t value = *reg;
    printf("PMIO[0x5B] @ 0x%08llX = 0x%02X\n",
           (unsigned long long)TARGET_PHYS, (unsigned int)value);
    printf("lower_nibble=0x%X policy_bits=%u mirror_nibble=0x%X\n",
           (unsigned int)(value & 0x0fU), (unsigned int)(value & 0x03U),
           (unsigned int)((value >> 4) & 0x0fU));

    if (munmap((void *)mapping, (size_t)page_size) != 0) {
        fprintf(stderr, "munmap: %s\n", strerror(errno));
        close(fd);
        return 5;
    }
    if (close(fd) != 0) {
        fprintf(stderr, "close: %s\n", strerror(errno));
        return 6;
    }
    return 0;
}
