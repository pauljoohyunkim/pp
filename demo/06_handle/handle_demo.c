/* Test double for a libc-style resource API.
 *
 * Models the shape of lib/pp_std/src/pp_print.c: raw descriptors plus a
 * struct-based write that mirrors `pp_std_print_string_ref` (a `const string&`
 * arrives here as a `pp_string_t*`). Closes are counted so the PP side can
 * assert that both handles are dropped.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "pp_std_types.h"

#define FILE_PATH "pp_handle_demo.txt"

static int32_t close_calls = 0;

/* handle File :: int32 drop pp_demo_close */
int32_t pp_demo_open_write(void) { return (int32_t)open(FILE_PATH, O_CREAT | O_WRONLY | O_TRUNC, 0644); }

int32_t pp_demo_open_read(void) { return (int32_t)open(FILE_PATH, O_RDONLY); }

int32_t pp_demo_write(int32_t fd, const pp_string_t* text) {
    if (fd < 0 || !text || !text->ptr) { return -1; }
    ssize_t written = write(fd, text->ptr, (size_t)text->len);
    return written == (ssize_t)text->len ? 0 : -1;
}

int32_t pp_demo_read(int32_t fd, uint8_t* buf, uint64_t len) {
    if (fd < 0 || !buf) { return -1; }
    ssize_t got = read(fd, buf, (size_t)len);
    return (int32_t)got;
}

void pp_demo_print(const uint8_t* buf, uint64_t len) {
    if (!buf || len == 0) { return; }
    fwrite(buf, 1, (size_t)len, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

void pp_demo_close(int32_t fd) {
    if (fd >= 0) { close(fd); }
    close_calls++;
}

int32_t pp_demo_close_count(void) { return close_calls; }
