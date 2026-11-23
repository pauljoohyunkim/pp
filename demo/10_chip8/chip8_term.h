#ifndef CHIP8_TERM_H
#define CHIP8_TERM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Returns an opaque token for the terminal state (never NULL). The matching
 * pp_term_close() restores the original terminal attributes. */
void* pp_term_open(void);

/* Drop function for the PP_Term handle: restores termios state and blocking mode. */
void pp_term_close(void* handle);

/* Polls pending stdin bytes without blocking and refreshes keys[0..count-1] (0 or 1).
 * A key stays down for a few frames after its last repeat byte because terminals
 * never report key-up. Returns non-zero when ESC was pressed (quit request). */
int32_t pp_term_poll_keys(uint8_t* keys, uint64_t count);

/* Sleeps for `ms` milliseconds (frame pacing). */
void pp_term_sleep_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* CHIP8_TERM_H */
