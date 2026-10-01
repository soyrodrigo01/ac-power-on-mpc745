#define _GNU_SOURCE
#include "pmio_identity.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int read_exact_line(const char *path, const char *expected) {
    char buffer[128];
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    const int close_ok = (close(fd) == 0);
    if (count <= 0 || !close_ok) return 0;
    if (memchr(buffer, '\0', (size_t)count) != NULL) return 0;

    size_t length = (size_t)count;
    while (length > 0U && (buffer[length - 1U] == '\n' || buffer[length - 1U] == '\r'))
        length--;

    const size_t expected_length = strlen(expected);
    return length == expected_length && memcmp(buffer, expected, length) == 0;
}

int pmio_identity_matches(const char *dmi_root) {
    if (dmi_root == NULL) return 0;
    char board[512];
    char bios[512];
    const int board_n = snprintf(board, sizeof(board), "%s/board_name", dmi_root);
    const int bios_n = snprintf(bios, sizeof(bios), "%s/bios_version", dmi_root);
    if (board_n <= 0 || (size_t)board_n >= sizeof(board) ||
        bios_n <= 0 || (size_t)bios_n >= sizeof(bios)) return 0;
    return read_exact_line(board, "RED-MPC745-1610BK") &&
           read_exact_line(bios, "MC3LY.03");
}
