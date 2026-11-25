#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pp_ast.h"
#include "pp_code_generator.h"
#include "pp_compiler_pipeline.h"
#include "pp_linker_offload.h"
#include "pp_string_pool.h"

#define PPLANG_OPT_EMIT_LLVM 1000
#define PPLANG_OPT_PPI 1001
#define PPLANG_OPT_HELP 1002
#define PPLANG_OPT_FREESTANDING 1003

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

typedef enum { PP_UNKNOWN, PP_SOURCE, PP_OBJECT } PPLang_Filetype;

typedef struct {
    char* src_path;
    char* obj_path;
    LLVMMemoryBufferRef obj_buf;
    PPLang_Filetype type;
} PPLang_Input;

typedef struct {
    PPLang_Input* inputs;
    unsigned int count;
    unsigned int capacity;
    LLVMTargetMachineRef target_machine;
} PPLang_InputList;

typedef struct {
    char** paths;
    unsigned int count;
    unsigned int capacity;
} PPLang_PPIList;

/* Static function prototypes */
static PPLang_InputList* pplang_input_list_create(void);
static void pplang_input_list_free(PPLang_InputList* list);
static int pplang_input_list_push(PPLang_InputList* list, PPLang_Input input);
static PPLang_PPIList* pplang_ppi_list_create(void);
static void pplang_ppi_list_free(PPLang_PPIList* list);
static int pplang_ppi_list_push(PPLang_PPIList* list, const char* path);
static PPLang_Filetype detect_filetype(const char* path);
static PPLang_Filetype detect_filetype(const char* path);
static void print_help(void);
static const char* pplang_get_temp_dir(void);

/* Main */
int main(int argc, char** argv) {
    int exit_code = 0;

    bool to_object = false;
    bool to_assembly = false;
    bool emit_ir = false;
    bool verbose = false;
    bool debug = false;
    bool freestanding = false;
    bool output_specified = false;
    char output_filename[BUFSIZ] = {0};
    strcpy(output_filename, "a.out");
    PPLang_InputList* input_list = pplang_input_list_create();
    PPLang_PPIList* ppi_list = pplang_ppi_list_create();
    PP_StringPool* string_pool = pp_string_pool_create();
    char optimization_mode = '\0';
    int opt;
    static struct option long_opts[] = {{"emit-llvm", no_argument, 0, PPLANG_OPT_EMIT_LLVM},
                                        {"ppi", required_argument, 0, PPLANG_OPT_PPI},
                                        {"help", no_argument, 0, PPLANG_OPT_HELP},
                                        {"ffreestanding", no_argument, 0, PPLANG_OPT_FREESTANDING},
                                        {0, 0, 0, 0}};
    while ((opt = getopt_long(argc, argv, "hcSvo:O:g", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'h':
            case PPLANG_OPT_HELP:
                print_help();
                exit_code = 1;
                goto error_cleanup;
            case 'c':
                to_object = true;
                break;
            case 'S':
                to_assembly = true;
                break;
            case PPLANG_OPT_EMIT_LLVM:
                emit_ir = true;
                break;
            case PPLANG_OPT_FREESTANDING:
                freestanding = true;
                break;
            case PPLANG_OPT_PPI:
                if (!ppi_list) {
                    fprintf(stderr, "PPI list not initialized\n");
                    exit_code = 1;
                    goto error_cleanup;
                }
                if (!pplang_ppi_list_push(ppi_list, optarg)) {
                    fprintf(stderr, "Failed to push PPI file %s\n", optarg);
                    exit_code = 1;
                    goto error_cleanup;
                }
                break;
            case 'v':
                verbose = true;
                break;
            case 'g':
                debug = true;
                break;
            case 'o':
                output_specified = true;
                memset(output_filename, 0, sizeof(output_filename));
                strncpy(output_filename, optarg, BUFSIZ - 1);
                break;
            case 'O':
                optimization_mode = optarg[0];
                break;
            default:
                exit_code = 1;
                goto error_cleanup;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "No source file given.\n");
        exit_code = 1;
        goto error_cleanup;
    }

    if (optimization_mode != '\0') {
        switch (optimization_mode) {
            case '0':
            case '1':
            case '2':
            case '3':
                break;
            case 's':
            case 'z':
                fprintf(stderr, "Warning: -O%c not supported yet, size optimization TODO\n", optimization_mode);
                optimization_mode = '\0';
                break;
            default:
                fprintf(stderr, "Invalid optimization mode. Allowed modes are -O0, -O1, -O2, -O3, -Os, and -Oz\n");
                exit_code = 1;
                goto error_cleanup;
        }
    }

    // Freestanding mode keeps the IR unoptimized: LoopIdiom and related passes at -O1+ can synthesise
    // llvm.memcpy/llvm.memset, which may lower to libcalls the target environment does not provide.
    if (freestanding && optimization_mode != '\0' && optimization_mode != '0') {
        fprintf(stderr,
                "Warning: --ffreestanding forces -O0; higher optimization levels can introduce "
                "memcpy/memset libcalls\n");
        optimization_mode = '\0';
    }

    // -S
    if (to_assembly && !output_specified) { strcpy(output_filename, "a.s"); }

    // --emit-llvm
    if (emit_ir && !output_specified) { strcpy(output_filename, "a.ll"); }

    // Translation unit -> Input list
    unsigned int source_count = 0;
    for (int i = optind; i < argc; i++) {
        PPLang_Input input;
        input.src_path = strdup(argv[i]);
        input.obj_path = NULL;
        input.obj_buf = NULL;
        input.type = detect_filetype(argv[i]);
        if (input.type == PP_SOURCE) {
            source_count++;
            if (to_object || to_assembly || emit_ir) {
                input.obj_path = strdup(output_filename);
            } else {
                /* Object will be materialised later via mkstemp */
                input.obj_path = NULL;
                input.obj_buf = NULL;
            }
        } else {
            input.obj_path = strdup(argv[i]);
            input.obj_buf = NULL;
        }
        if (!pplang_input_list_push(input_list, input)) {
            fprintf(stderr, "Failed to push input\n");
            exit_code = 1;
            goto error_cleanup;
        }
    }

    // -c, -S, --emit-llvm can only take one source file.
    if ((to_object || to_assembly || emit_ir) && source_count != 1) {
        fprintf(stderr, "-c/-S requires exactly one source file, got %u\n", source_count);
        exit_code = 1;
        goto error_cleanup;
    }

    // -c -S is a conflict.
    if (to_object && to_assembly) {
        fprintf(stderr, "Cannot specify both -c and -S\n");
        exit_code = 1;
        goto error_cleanup;
    }

    // Create a single target machine for optimization and emission
    {
        LLVMContextRef tmp_ctx = LLVMContextCreate();
        LLVMModuleRef tmp_mod = LLVMModuleCreateWithNameInContext("tmp", tmp_ctx);
        if (!pp_code_generator_target_machine_create_default(tmp_mod, &input_list->target_machine)) {
            fprintf(stderr, "Failed to create target machine\n");
            LLVMDisposeModule(tmp_mod);
            LLVMContextDispose(tmp_ctx);
            exit_code = 1;
            goto error_cleanup;
        }
        LLVMDisposeModule(tmp_mod);
        LLVMContextDispose(tmp_ctx);
    }

    CompilerPipelineOptions opts = {
        .string_pool = string_pool,
        .ppi_paths = (const char**)ppi_list->paths,
        .ppi_count = ppi_list->count,
        .target_machine = input_list->target_machine,
        .verbose = verbose,
        .optimization_mode = optimization_mode,
        .to_assembly = to_assembly,
        .emit_ir = emit_ir,
        .debug = debug,
        .freestanding = freestanding,
    };

    /*
    For each input,
        if source file
            if compile_to_object
                compile to object
            else
                compile to buffer
        else
            do nothing
    */
    for (unsigned int i = 0; i < input_list->count; i++) {
        PPLang_Input* inp = &input_list->inputs[i];
        if (inp->type == PP_SOURCE) {
            if (to_object || to_assembly || emit_ir) {
                if (inp->obj_path) {
                    if (!pp_compiler_pipeline_compile_source_opts(&opts, inp->src_path, inp->obj_path)) {
                        fprintf(stderr, "Compilation failed for %s\n", inp->src_path);
                        exit_code = 1;
                        goto error_cleanup;
                    }
                }
            } else {
                LLVMMemoryBufferRef buf = NULL;
                if (!pp_compiler_pipeline_compile_source_to_buffer_opts(&opts, inp->src_path, &buf)) {
                    fprintf(stderr, "Compilation failed for %s\n", inp->src_path);
                    exit_code = 1;
                    goto error_cleanup;
                }
                inp->obj_buf = buf;
            }
        }
    }

    if (!to_object) {
        /* Materialise in-memory objects to safe temp files for linking */
        unsigned int obj_count = 0;
        for (unsigned int i = 0; i < input_list->count; i++) {
            if (input_list->inputs[i].type == PP_OBJECT) {
                obj_count++;
            } else if (input_list->inputs[i].type == PP_SOURCE && input_list->inputs[i].obj_buf) {
                obj_count++;
            }
        }
        if (obj_count > 0) {
            const char** obj_names = (const char**)calloc(obj_count, sizeof(char*));
            unsigned int idx = 0;
            for (unsigned int i = 0; i < input_list->count; i++) {
                PPLang_Input* inp = &input_list->inputs[i];
                if (inp->type == PP_OBJECT) {
                    obj_names[idx++] = inp->obj_path;
                } else if (inp->type == PP_SOURCE && inp->obj_buf) {
                    const char* tmpdir = pplang_get_temp_dir();
                    char tmpl[PATH_MAX];
                    snprintf(tmpl, sizeof(tmpl), "%s/pplang-XXXXXX", tmpdir);
                    int fd = mkstemp(tmpl);
                    if (fd < 0) {
                        fprintf(stderr, "Failed to create temp object file\n");
                        free(obj_names);
                        exit_code = 1;
                        goto error_cleanup;
                    }
                    size_t sz = LLVMGetBufferSize(inp->obj_buf);
                    const char* data = LLVMGetBufferStart(inp->obj_buf);
                    ssize_t written = 0;
                    size_t to_write = sz;
                    while (to_write > 0) {
                        ssize_t w = write(fd, data + written, to_write);
                        if (w <= 0) {
                            close(fd);
                            unlink(tmpl);
                            free(obj_names);
                            exit_code = 1;
                            goto error_cleanup;
                        }
                        written += w;
                        to_write -= w;
                    }
                    close(fd);
                    /* Append .o extension for linker */
                    char obj_path_buf[PATH_MAX];
                    snprintf(obj_path_buf, sizeof(obj_path_buf), "%s.o", tmpl);
                    /* Rename temp file to have .o extension */
                    if (rename(tmpl, obj_path_buf) != 0) {
                        unlink(tmpl);
                        fprintf(stderr, "Failed to rename temp object file\n");
                        free(obj_names);
                        exit_code = 1;
                        goto error_cleanup;
                    }
                    inp->obj_path = strdup(obj_path_buf);
                    obj_names[idx++] = inp->obj_path;
                    /* Buffer can be freed now; temp file holds the object */
                    LLVMDisposeMemoryBuffer(inp->obj_buf);
                    inp->obj_buf = NULL;
                }
            }
            if (!pp_linker_offload_multi(obj_names, obj_count, output_filename, optimization_mode)) {
                fprintf(stderr, "Linker failed\n");
                free(obj_names);
                exit_code = 1;
                goto error_cleanup;
            }
            free(obj_names);
        }
        goto exit_success;
    }

    if (to_object || to_assembly || emit_ir) { goto exit_success; }

exit_success:
    pplang_input_list_free(input_list);
    pplang_ppi_list_free(ppi_list);
    pp_string_pool_free(string_pool);
    return exit_code;

error_cleanup:
    pplang_input_list_free(input_list);
    pplang_ppi_list_free(ppi_list);
    pp_string_pool_free(string_pool);
    return exit_code;
}

/* Static function definitions */

static PPLang_InputList* pplang_input_list_create(void) {
    PPLang_InputList* list = (PPLang_InputList*)calloc(1, sizeof(PPLang_InputList));
    if (!list) return NULL;
    list->capacity = 4;
    list->inputs = (PPLang_Input*)calloc(list->capacity, sizeof(PPLang_Input));
    if (!list->inputs) {
        free(list);
        return NULL;
    }
    list->count = 0;
    return list;
}

static void pplang_input_list_free(PPLang_InputList* list) {
    if (!list) return;
    if (list->inputs) {
        for (unsigned int i = 0; i < list->count; i++) {
            if (list->inputs[i].src_path) free(list->inputs[i].src_path);
            if (list->inputs[i].obj_path) {
                /* Unlink temporary object files if they exist */
                const char* tmpdir = pplang_get_temp_dir();
                size_t tmpdir_len = strlen(tmpdir);
                if (strncmp(list->inputs[i].obj_path, tmpdir, tmpdir_len) == 0 &&
                    list->inputs[i].obj_path[tmpdir_len] == '/' &&
                    strncmp(list->inputs[i].obj_path + tmpdir_len + 1, "pplang-", 7) == 0) {
                    unlink(list->inputs[i].obj_path);
                }
                free(list->inputs[i].obj_path);
            }
            if (list->inputs[i].obj_buf) { LLVMDisposeMemoryBuffer(list->inputs[i].obj_buf); }
        }
        free(list->inputs);
    }
    if (list->target_machine) { pp_code_generator_target_machine_free(list->target_machine); }
    free(list);
}

static int pplang_input_list_push(PPLang_InputList* list, PPLang_Input input) {
    PPLang_Input* new_inputs = NULL;
    unsigned int new_cap = 0;
    NULL_POINTER_ERROR_HANDLE(list);
    if (list->count >= list->capacity) {
        new_cap = list->capacity * 2;
        new_inputs = (PPLang_Input*)realloc(list->inputs, new_cap * sizeof(PPLang_Input));
        NULL_POINTER_ERROR_HANDLE(new_inputs);
        list->inputs = new_inputs;
        list->capacity = new_cap;
    }
    list->inputs[list->count] = input;
    list->count++;
    return 1;
error_cleanup:
    return 0;
}

static PPLang_PPIList* pplang_ppi_list_create(void) {
    PPLang_PPIList* list = (PPLang_PPIList*)calloc(1, sizeof(PPLang_PPIList));
    if (!list) return NULL;
    list->capacity = 4;
    list->paths = (char**)calloc(list->capacity, sizeof(char*));
    if (!list->paths) {
        free(list);
        return NULL;
    }
    list->count = 0;
    return list;
}

static void pplang_ppi_list_free(PPLang_PPIList* list) {
    if (!list) return;
    if (list->paths) {
        for (unsigned int i = 0; i < list->count; i++) {
            if (list->paths[i]) free(list->paths[i]);
        }
        free(list->paths);
    }
    free(list);
}

static int pplang_ppi_list_push(PPLang_PPIList* list, const char* path) {
    NULL_POINTER_ERROR_HANDLE(list);
    NULL_POINTER_ERROR_HANDLE(path);
    if (list->count >= list->capacity) {
        unsigned int new_cap = list->capacity * 2;
        char** tmp = (char**)realloc(list->paths, new_cap * sizeof(char*));
        NULL_POINTER_ERROR_HANDLE(tmp);
        list->paths = tmp;
        list->capacity = new_cap;
    }
    list->paths[list->count++] = strdup(path);
    return 1;
error_cleanup:
    return 0;
}

static PPLang_Filetype detect_filetype(const char* path) {
    FILE* fp = NULL;
    unsigned char magic[4] = {0};
    size_t r = 0;

    if (!path) return PP_UNKNOWN;

    const char* dot = strrchr(path, '.');
    if (dot) {
        if (strcmp(dot, ".pp") == 0) return PP_SOURCE;
        if (strcmp(dot, ".o") == 0 || strcmp(dot, ".obj") == 0 || strcmp(dot, ".a") == 0 || strcmp(dot, ".so") == 0 ||
            strcmp(dot, ".dylib") == 0) {
            return PP_OBJECT;
        }
    }

    fp = fopen(path, "rb");
    if (!fp) return PP_UNKNOWN;

    r = fread(magic, 1, 4, fp);
    if (fp) fclose(fp);
    if (r >= 4) {
        if (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') { return PP_OBJECT; }
        if ((magic[0] == 0xfe && magic[1] == 0xed && magic[2] == 0xfa && magic[3] == 0xed) ||
            (magic[0] == 0xce && magic[1] == 0xfa && magic[2] == 0xed && magic[3] == 0xfe)) {
            return PP_OBJECT;
        }
        if (magic[0] == 0x4d && magic[1] == 0x5a) { return PP_OBJECT; }
    }
    return PP_SOURCE;
}

static void print_help(void) {
    printf("Usage: pplang [-h] [-v] [-c] [-S] [-o filename] [-O 0123] [--ppi file] [-I dir] src.pp\n");
    printf("-h\t\t\t\tShow this help menu\n");
    printf("-v\t\t\t\tShow verbose output\n");
    printf("-g\t\t\t\tEmit DWARF debug information\n");
    printf("-c\t\t\t\tCompile to object file.\n");
    printf("-S\t\t\t\tCompile to assembly file.\n");
    printf("--emit-llvm\t\t\tEmit LLVM IR.\n");
    printf("--ppi file\t\t\tPre-processor include file for external PPI declarations, can be repeated.\n");
    printf("--ffreestanding\t\t\tCompile for a heap-free environment: reject lists and strings.\n");
    printf("-o filename\t\t\tOutput filename (default: a.out)\n");
    printf(
        "-O level\t\t\tOptimization level: 0=no opt, 1=less, 2=default, 3=aggressive. Size optimisation (-Os, -Oz) "
        "TODO\n");
}

static const char* pplang_get_temp_dir(void) {
    const char* tmpdir = getenv("TMPDIR");
    return tmpdir ? tmpdir : "/tmp";
}
