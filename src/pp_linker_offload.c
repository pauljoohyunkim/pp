#include <unistd.h>
#include <sys/wait.h>

#include "pp_linker_offload.h"

int pp_linker_offload(const char* input_object_filename, const char* output_filename) {

    // Subprocess
    pid_t pid = fork();

    if (pid < 0) return 0;

    if (pid > 0) {
        // Parent
        wait(NULL);
    } else {
        // Child

        char* command[] = { "cc", input_object_filename, "-o", output_filename, NULL };
        execvp("cc", command);

        return 0;
    }

    return 1;
}