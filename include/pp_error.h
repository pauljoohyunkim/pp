#ifndef PP_ERROR_H
#define PP_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "pp_source_loc.h"

typedef struct {
    char* message;
    PP_SourceLoc loc;
} PP_Error;

typedef struct {
    PP_Error* errors;
    size_t n_errors;
    size_t error_capacity;
} PP_ErrorList;

PP_ErrorList* pp_error_list_create(void);
int pp_error_list_push_error(PP_ErrorList* error_list, PP_SourceLoc loc, const char* format,
                             ...);
void pp_error_list_free(PP_ErrorList* error_list);

#ifdef __cplusplus
}
#endif

#endif /* PP_ERROR_H */
