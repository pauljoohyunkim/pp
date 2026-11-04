#include "pp_linker_offload.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int pp_linker_offload(const char* input_object_filename, const char* output_filename, char optimization_mode) {
    // Subprocess
    pid_t pid = fork();

    if (pid < 0) return 0;

    if (pid > 0) {
        // Parent
        wait(NULL);
    } else {
        // Child

        char opt_flag[4] = {0};
        char* argv[6];
        int idx = 0;
        argv[idx++] = "cc";
        argv[idx++] = (char*)input_object_filename;
        if (optimization_mode != '\0') {
            snprintf(opt_flag, sizeof(opt_flag), "-O%c", optimization_mode);
            argv[idx++] = opt_flag;
        }
        argv[idx++] = "-o";
        argv[idx++] = (char*)output_filename;
        argv[idx++] = NULL;
        execvp("cc", argv);

        return 0;
    }

    return 1;
}

int pp_linker_offload_multi(const char** object_filenames, unsigned int count, const char* output_filename,
                            char optimization_mode) {
    if (!object_filenames || count == 0 || !output_filename) return 0;

    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid > 0) {
        wait(NULL);
    } else {
        // Build argv for cc: cc obj1 obj2 ... [-O<opt>] -o out
        unsigned int argc = count + 4 + 1;
        char** argv = (char**)calloc(argc, sizeof(char*));
        if (!argv) _exit(1);
        argv[0] = "cc";
        for (unsigned int i = 0; i < count; i++) { argv[i + 1] = (char*)object_filenames[i]; }
        unsigned int idx = count + 1;
        if (optimization_mode != '\0') {
            char opt_flag[4];
            snprintf(opt_flag, sizeof(opt_flag), "-O%c", optimization_mode);
            argv[idx++] = opt_flag;
        }
        argv[idx++] = "-o";
        argv[idx++] = (char*)output_filename;
        argv[idx] = NULL;
        execvp("cc", argv);
        free(argv);
        _exit(1);
    }
    return 1;
}