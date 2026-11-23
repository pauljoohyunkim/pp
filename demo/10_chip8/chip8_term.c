/* chip8_term.c - terminal keyboard helpers for the CHIP-8 demo.
 *
 * Terminals are line-buffered and blocking by default, and they report key-down
 * (plus auto-repeat) but never key-up. This file switches stdin to raw,
 * non-blocking mode and turns those bytes into a CHIP-8 keypad snapshot.
 *
 * Build: the demo Makefile compiles this file into chip8_term.o and passes the
 * object to pplang for linking. See chip8_term.ppi for the PP side. */

#include "chip8_term.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CHIP8_TERM_KEY_COUNT 16

/* How many polled frames a key stays "down" after its last byte arrived. A tap
 * is a single byte, so without this decay it would be visible for one frame
 * only. Called once per frame, 3 frames is ~50 ms at 60 Hz. */
#define CHIP8_TERM_HOLD_FRAMES 3

typedef struct {
    int active;
    int is_tty;
    int saved_flags;
    struct termios saved_termios;
    uint8_t hold[CHIP8_TERM_KEY_COUNT];
} chip8_term_state_t;

static chip8_term_state_t g_term;

/* Keyboard layout -> CHIP-8 keypad:
 *   1 2 3 4       1 2 3 C
 *   q w e r  =>   4 5 6 D
 *   a s d f       7 8 9 E
 *   z x c v       A 0 B F  */
static int chip8_term_map_key(unsigned char ch) {
    switch (ch) {
        case '1':
            return 0x1;
        case '2':
            return 0x2;
        case '3':
            return 0x3;
        case '4':
            return 0xC;
        case 'q':
            return 0x4;
        case 'w':
            return 0x5;
        case 'e':
            return 0x6;
        case 'r':
            return 0xD;
        case 'a':
            return 0x7;
        case 's':
            return 0x8;
        case 'd':
            return 0x9;
        case 'f':
            return 0xE;
        case 'z':
            return 0xA;
        case 'x':
            return 0x0;
        case 'c':
            return 0xB;
        case 'v':
            return 0xF;
        default:
            return -1;
    }
}

static void chip8_term_restore(void) {
    if (!g_term.active) return;
    if (g_term.is_tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_term.saved_termios);
        g_term.is_tty = 0;
    }
    if (g_term.saved_flags != -1) {
        fcntl(STDIN_FILENO, F_SETFL, g_term.saved_flags);
        g_term.saved_flags = -1;
    }
    g_term.active = 0;
}

/* The handle drop runs on normal scope exit; atexit covers early returns and
 * signal-free abnormal exits that bypass the drop. */
static void chip8_term_atexit(void) { chip8_term_restore(); }

void* pp_term_open(void) {
    if (g_term.active) return &g_term; /* one terminal state per process */

    memset(&g_term, 0, sizeof(g_term));
    g_term.saved_flags = -1;

    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_term.saved_termios) == 0) {
        struct termios raw = g_term.saved_termios;
        raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) { g_term.is_tty = 1; }
    }

    /* Non-blocking even when stdin is a pipe, so the emulator loop never stalls. */
    g_term.saved_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (g_term.saved_flags != -1) { fcntl(STDIN_FILENO, F_SETFL, g_term.saved_flags | O_NONBLOCK); }

    g_term.active = 1;
    atexit(chip8_term_atexit);
    return &g_term;
}

void pp_term_close(void* handle) {
    (void)handle; /* the state is process-global */
    chip8_term_restore();
}

int32_t pp_term_poll_keys(uint8_t keys[16]) {
    size_t n = CHIP8_TERM_KEY_COUNT;
    unsigned char ch;
    int32_t quit = 0;
    size_t i = 0;

    if (!keys || n == 0) return 0;

    /* Decay anything held from previous frames. */
    for (i = 0; i < n; i++) {
        if (g_term.hold[i] > 0) g_term.hold[i]--;
        keys[i] = g_term.hold[i] ? 1 : 0;
    }

    while (read(STDIN_FILENO, &ch, 1) == 1) {
        int key = chip8_term_map_key(ch);
        if (ch == 0x1B) { /* ESC quits the emulator */
            quit = 1;
            continue;
        }
        if (key >= 0 && (size_t)key < n) {
            g_term.hold[key] = CHIP8_TERM_HOLD_FRAMES;
            keys[key] = 1;
        }
    }

    return quit;
}

void pp_term_sleep_ms(uint32_t ms) {
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&req, &req) == -1) {
        /* Interrupted by a signal: resume the remaining time. */
    }
}
