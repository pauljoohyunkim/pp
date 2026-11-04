#ifndef PP_LINKER_OFFLOAD_H
#define PP_LINKER_OFFLOAD_H

#ifdef __cplusplus
extern "C" {
#endif

int pp_linker_offload(const char* input_object_filename, const char* output_filename, char optimization_mode);

int pp_linker_offload_multi(const char** object_filenames, unsigned int count, const char* output_filename,
                            char optimization_mode);

#ifdef __cplusplus
}
#endif

#endif /* PP_LINKER_OFFLOAD_H */
