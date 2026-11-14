#ifndef PP_SOURCE_LOC_H
#define PP_SOURCE_LOC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* filename;
    unsigned int line;
    unsigned int column;
} PP_SourceLoc;

#ifdef __cplusplus
}
#endif

#endif /* PP_SOURCE_LOC_H */
