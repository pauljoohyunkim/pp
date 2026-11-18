/* Test double for a libc-style resource API.
 * Counts how many times each resource was released so the PP side can assert
 * that every handle is dropped exactly once. */
static int close_calls = 0;
static int free_calls = 0;

int pp_demo_open(void) { return 42; }
void pp_demo_close(int fd) { (void)fd; close_calls++; }
int pp_demo_close_count(void) { return close_calls; }

void* pp_demo_null_ptr(void) { return 0; }
void pp_demo_free(void* p) { (void)p; free_calls++; }
int pp_demo_free_count(void) { return free_calls; }
