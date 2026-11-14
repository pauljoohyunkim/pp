#include "pp_error.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PP_ErrorList* pp_error_list_create(void) {
    PP_ErrorList* error_list = (PP_ErrorList*)calloc(1, sizeof(PP_ErrorList));
    if (!error_list) return NULL;
    error_list->error_capacity = 8;
    error_list->errors = (PP_Error*)calloc(error_list->error_capacity, sizeof(PP_Error));
    if (!error_list->errors) {
        free(error_list);
        return NULL;
    }
    error_list->n_errors = 0;
    return error_list;
}

int pp_error_list_push_error(PP_ErrorList* error_list, PP_SourceLoc loc, const char* format, ...) {
    if (!error_list) return 0;
    if (error_list->n_errors > error_list->error_capacity) return 0;

    va_list args;
    va_start(args, format);
    char buffer[BUFSIZ] = {0};
    int msg_len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (msg_len < 0) {
        fprintf(stderr, "[-] Internal error: logging failure (vsnprintf failure)\n");
        return 0;
    }
    char* msg = (char*)calloc(msg_len + 1, sizeof(char));
    if (!msg) {
        fprintf(stderr, "[-] Internal error: logging failure (msg allocation failure)\n");
        return 0;
    }
    memcpy(msg, buffer, msg_len + 1);

    if (error_list->n_errors == error_list->error_capacity) {
        PP_Error* new_errors =
            (PP_Error*)realloc(error_list->errors, sizeof(PP_Error) * error_list->error_capacity * 2);
        if (!new_errors) {
            free(msg);
            return 0;
        }
        error_list->errors = new_errors;
        error_list->error_capacity *= 2;
    }

    PP_Error err = {.message = msg};
    err.loc = loc;
    error_list->errors[error_list->n_errors] = err;
    error_list->n_errors++;
    return 1;
}

void pp_error_list_free(PP_ErrorList* error_list) {
    if (!error_list) return;
    for (size_t i = 0; i < error_list->n_errors; i++) { free(error_list->errors[i].message); }
    free(error_list->errors);
    free(error_list);
}
