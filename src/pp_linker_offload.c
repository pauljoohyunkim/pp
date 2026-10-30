#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

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

int pp_linker_offload_multi(const char** object_filenames, unsigned int count, const char* output_filename) {
    if (!object_filenames || count == 0 || !output_filename) return 0;

    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid > 0) {
        wait(NULL);
    } else {
        // Build argv for cc: cc obj1 obj2 ... -o out
        unsigned int argc = count + 3;
        char** argv = (char**)calloc(argc, sizeof(char*));
        if (!argv) _exit(1);
        argv[0] = "cc";
        for (unsigned int i = 0; i < count; i++) {
            argv[i + 1] = (char*)object_filenames[i];
        }
        argv[count + 1] = "-o";
        argv[count + 2] = (char*)output_filename;
        execvp("cc", argv);
        free(argv);
        _exit(1);
    }
    return 1;
}