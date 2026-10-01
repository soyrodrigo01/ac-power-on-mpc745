#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../pmio_identity.h"

static void write_blob(const char *path, const void *data, size_t length) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    assert(fd >= 0);
    assert(write(fd, data, length) == (ssize_t)length);
    assert(close(fd) == 0);
}

static void write_text(const char *path, const char *text) {
    write_blob(path, text, strlen(text));
}

static void test_exact_board_and_bios_are_required(void) {
    char root[] = "/tmp/pmio-dmi-test-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char board[512], bios[512];
    assert(snprintf(board, sizeof(board), "%s/board_name", root) > 0);
    assert(snprintf(bios, sizeof(bios), "%s/bios_version", root) > 0);

    write_text(board, "RED-MPC745-1610BK\n");
    write_text(bios, "MC3LY.03\n");
    assert(pmio_identity_matches(root) == 1);

    write_text(bios, "MC3LY.04\n");
    assert(pmio_identity_matches(root) == 0);

    write_text(bios, "MC3LY.03\n");
    write_text(board, "RED-MPC745-1610BK-LOOKALIKE\n");
    assert(pmio_identity_matches(root) == 0);

    const char leading_nul[] = {'\0', 'x', '\n'};
    write_blob(board, leading_nul, sizeof(leading_nul));
    assert(pmio_identity_matches(root) == 0);

    const char embedded_nul[] = "RED-MPC745-1610BK\0LOOKALIKE\n";
    write_blob(board, embedded_nul, sizeof(embedded_nul) - 1U);
    assert(pmio_identity_matches(root) == 0);

    assert(unlink(board) == 0);
    assert(unlink(bios) == 0);
    assert(rmdir(root) == 0);
}

int main(void) {
    test_exact_board_and_bios_are_required();
    puts("test_pmio_identity: PASS");
    return 0;
}
