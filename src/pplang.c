#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pp_ast.h"
#include "pp_code_generator.h"
#include "pp_code_optimization.h"
#include "pp_lexer.h"
#include "pp_linker_offload.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"

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

/* Static function prototypes */
static PPLang_InputList* pplang_input_list_create(void);
static void pplang_input_list_free(PPLang_InputList* list);
static int pplang_input_list_push(PPLang_InputList* list, PPLang_Input input);
static PPLang_Filetype detect_filetype(const char* path);
static int compile_pp_source(const char* src_path, const char* obj_path, bool verbose, char optimization_mode, LLVMTargetMachineRef tm);
static int compile_pp_source_to_buffer(const char* src_path, LLVMMemoryBufferRef* out_buf, bool verbose, char optimization_mode, LLVMTargetMachineRef tm);
static int compile_pp_source_core(const char* src_path, bool verbose, PP_CodeGenerator** out_cg);
static long get_file_size(const char* filename);
static void print_help(void);

/* Main */
int main(int argc, char** argv) {
    int exit_code = 0;

    bool to_object = false;
    bool verbose = false;
    char output_filename[BUFSIZ] = {0};
    strcpy(output_filename, "a.out");
    PPLang_InputList* input_list = NULL;
    char optimization_mode =  '\0';
    int opt;
    while ((opt = getopt(argc, argv, "hcvo:O:")) != -1) {
        switch (opt) {
            case 'h':
                print_help();
                return 1;
            case 'c':
                to_object = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'o':
                memset(output_filename, 0, sizeof(output_filename));
                strncpy(output_filename, optarg, BUFSIZ - 1);
                break;
            case 'O':
                optimization_mode = optarg[0];
                break;
            default:
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "No source file given.\n");
        return 1;
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
                return 1;
        }
    }

    input_list = pplang_input_list_create();
    if (!input_list) {
        fprintf(stderr, "Failed to create input list\n");
        return 1;
    }
    unsigned int source_count = 0;
    for (int i = optind; i < argc; i++) {
        PPLang_Input input;
        input.src_path = strdup(argv[i]);
        input.obj_path = NULL;
        input.obj_buf = NULL;
        input.type = detect_filetype(argv[i]);
        if (input.type == PP_SOURCE) {
            source_count++;
            if (to_object) {
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
            pplang_input_list_free(input_list);
            return 1;
        }
    }

    if (to_object && source_count != 1) {
        fprintf(stderr, "-c requires exactly one source file, got %u\n", source_count);
        pplang_input_list_free(input_list);
        return 1;
    }

    if (optimization_mode != '\0') {
        LLVMContextRef tmp_ctx = LLVMContextCreate();
        LLVMModuleRef tmp_mod = LLVMModuleCreateWithNameInContext("tmp", tmp_ctx);
        if (!pp_code_generator_target_machine_create_default(tmp_mod, &input_list->target_machine)) {
            fprintf(stderr, "Failed to create target machine for optimization\n");
            LLVMDisposeModule(tmp_mod);
            LLVMContextDispose(tmp_ctx);
            pplang_input_list_free(input_list);
            return 1;
        }
        LLVMDisposeModule(tmp_mod);
        LLVMContextDispose(tmp_ctx);
    }

    for (unsigned int i = 0; i < input_list->count; i++) {
        PPLang_Input* inp = &input_list->inputs[i];
        if (inp->type == PP_SOURCE) {
            if (to_object) {
                if (inp->obj_path) {
                    if (!compile_pp_source(inp->src_path, inp->obj_path, verbose, optimization_mode, input_list->target_machine)) {
                        fprintf(stderr, "Compilation failed for %s\n", inp->src_path);
                        pplang_input_list_free(input_list);
                        return 1;
                    }
                }
            } else {
                LLVMMemoryBufferRef buf = NULL;
                if (!compile_pp_source_to_buffer(inp->src_path, &buf, verbose, optimization_mode, input_list->target_machine)) {
                    fprintf(stderr, "Compilation failed for %s\n", inp->src_path);
                    pplang_input_list_free(input_list);
                    return 1;
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
                    char tmpl[] = "/tmp/pplang-XXXXXX";
                    int fd = mkstemp(tmpl);
                    if (fd < 0) {
                        fprintf(stderr, "Failed to create temp object file\n");
                        free(obj_names);
                        pplang_input_list_free(input_list);
                        return 1;
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
                            pplang_input_list_free(input_list);
                            return 1;
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
                        pplang_input_list_free(input_list);
                        return 1;
                    }
                    inp->obj_path = strdup(obj_path_buf);
                    obj_names[idx++] = inp->obj_path;
                    /* Buffer can be freed now; temp file holds the object */
                    LLVMDisposeMemoryBuffer(inp->obj_buf);
                    inp->obj_buf = NULL;
                }
            }
            pp_linker_offload_multi(obj_names, obj_count, output_filename, optimization_mode);
            free(obj_names);
        }
        pplang_input_list_free(input_list);
        return exit_code;
    }

    if (to_object) {
        pplang_input_list_free(input_list);
        return exit_code;
    }
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
                if (strncmp(list->inputs[i].obj_path, "/tmp/pplang-", 11) == 0) { unlink(list->inputs[i].obj_path); }
                free(list->inputs[i].obj_path);
            }
            if (list->inputs[i].obj_buf) { LLVMDisposeMemoryBuffer(list->inputs[i].obj_buf); }
        }
        free(list->inputs);
    }
    if (list->target_machine) {
        pp_code_generator_target_machine_free(list->target_machine);
    }
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

static int compile_pp_source_core(const char* src_path, bool verbose, PP_CodeGenerator** out_cg) {
    FILE* fp = NULL;
    char* code = NULL;
    PP_Lexer* lexer = NULL;
    PP_Parser* parser = NULL;
    PP_SemanticAnalyzer* sa = NULL;
    PP_CodeGenerator* cg = NULL;
    long filesize = 0;
    size_t total_errors = 0;
    bool generate_code = true;

    NULL_POINTER_ERROR_HANDLE(src_path);
    NULL_POINTER_ERROR_HANDLE(out_cg);
    *out_cg = NULL;

    filesize = get_file_size(src_path);
    if (filesize < 0) {
        fprintf(stderr, "Could not open %s\n", src_path);
        goto error_cleanup;
    }

    fp = fopen(src_path, "r");
    if (!fp) {
        fprintf(stderr, "Could not open %s\n", src_path);
        goto error_cleanup;
    }
    code = (char*)calloc(filesize + 1, sizeof(char));
    if (!code) {
        fprintf(stderr, "Could not allocate memory for code.\n");
        goto error_cleanup;
    }
    fread(code, sizeof(char), filesize, fp);
    fclose(fp);
    fp = NULL;

    lexer = pp_lexer_create(code, src_path);
    free(code);
    code = NULL;
    NULL_POINTER_ERROR_HANDLE(lexer);
    if (!pp_lexer_analyze(lexer)) {
        fprintf(stderr, "Lexer analyze failure\n");
        goto error_cleanup;
    }
    if (lexer->error_list && lexer->error_list->n_errors > 0) {
        total_errors += lexer->error_list->n_errors;
        generate_code = false;
        for (size_t i = 0; i < lexer->error_list->n_errors; i++) {
            PP_Error* e = &lexer->error_list->errors[i];
            fprintf(stderr, "Lexer: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>", e->line,
                    e->column, e->message);
        }
    }

    parser = pp_parser_create(lexer);
    NULL_POINTER_ERROR_HANDLE(parser);
    if (pp_parser_parse(parser) != 1) { fprintf(stderr, "Parser failed\n"); }
    if (parser->error_list && parser->error_list->n_errors > 0) {
        total_errors += parser->error_list->n_errors;
        generate_code = false;
        for (size_t i = 0; i < parser->error_list->n_errors; i++) {
            PP_Error* e = &parser->error_list->errors[i];
            fprintf(stderr, "Parser: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>", e->line,
                    e->column, e->message);
        }
    }
    if (verbose && parser->error_list && parser->error_list->n_errors == 0) {
        printf("Parse succeeded. AST:\n");
        pp_ast_root_print(parser->program, 0);
    }

    sa = pp_semantic_analyzer_create(parser);
    NULL_POINTER_ERROR_HANDLE(sa);
    if (pp_semantic_analyzer_analyze(sa) != 1) { fprintf(stderr, "Semantic analysis failed\n"); }
    if (sa->error_list && sa->error_list->n_errors > 0) {
        total_errors += sa->error_list->n_errors;
        generate_code = false;
        for (size_t i = 0; i < sa->error_list->n_errors; i++) {
            PP_Error* e = &sa->error_list->errors[i];
            fprintf(stderr, "Semantic Analyzer: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>",
                    e->line, e->column, e->message);
        }
    }
    if (verbose && sa->error_list && sa->error_list->n_errors == 0) { printf("Semantic analysis succeeded.\n"); }

    if (!generate_code) {
        fprintf(stderr, "Skipping code generation due to %zu errors\n", total_errors);
        goto error_cleanup;
    }

    cg = pp_code_generator_create(sa);
    NULL_POINTER_ERROR_HANDLE(cg);
    if (verbose) { printf("Code generator created successfully.\n"); }
    if (!pp_code_generator_generate(cg)) {
        fprintf(stderr, "Code generation failure\n");
        goto error_cleanup;
    }
    if (verbose) { printf("Code generation succeeded.\n"); }

    *out_cg = cg;
    cg = NULL;
    pp_semantic_analyzer_free(sa);
    sa = NULL;
    pp_parser_free(parser);
    parser = NULL;
    pp_lexer_free(lexer);
    lexer = NULL;
    return 1;

error_cleanup:
    pp_code_generator_free(cg);
    pp_semantic_analyzer_free(sa);
    pp_parser_free(parser);
    pp_lexer_free(lexer);
    if (code) free(code);
    if (fp) fclose(fp);
    *out_cg = NULL;
    return 0;
}

static int compile_pp_source(const char* src_path, const char* obj_path, bool verbose, char optimization_mode, LLVMTargetMachineRef tm) {
    PP_CodeGenerator* cg = NULL;
    if (!compile_pp_source_core(src_path, verbose, &cg)) return 0;
    int ok = 0;
    if (optimization_mode != '\0' && tm != NULL) {
        if (!pp_optimize_module(cg->mod, tm, optimization_mode)) {
            fprintf(stderr, "Optimization failed\n");
            pp_code_generator_free(cg);
            return 0;
        }
    }
    ok = pp_code_generator_emit_object_file(cg->mod, obj_path);
    pp_code_generator_free(cg);
    return ok;
}

static int compile_pp_source_to_buffer(const char* src_path, LLVMMemoryBufferRef* out_buf, bool verbose, char optimization_mode, LLVMTargetMachineRef tm) {
    if (!out_buf) return 0;
    PP_CodeGenerator* cg = NULL;
    if (!compile_pp_source_core(src_path, verbose, &cg)) return 0;
    if (optimization_mode != '\0' && tm != NULL) {
        if (!pp_optimize_module(cg->mod, tm, optimization_mode)) {
            fprintf(stderr, "Optimization failed\n");
            pp_code_generator_free(cg);
            return 0;
        }
    }
    LLVMMemoryBufferRef buf = NULL;
    if (!pp_code_generator_emit_object_to_buffer(cg->mod, &buf)) {
        pp_code_generator_free(cg);
        return 0;
    }
    *out_buf = buf;
    pp_code_generator_free(cg);
    return 1;
}

static long get_file_size(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    fclose(fp);
    return size;
}

static void print_help(void) {
    printf("Usage: pplang [-h] [-v] [-c] [-o filename] [-O 0123] src.pp\n");
    printf("-h\t\t\t\tShow this help menu\n");
    printf("-v\t\t\t\tShow verbose output\n");
    printf("-c\t\t\t\tCompile to object file.\n");
    printf("-o filename\t\t\tOutput filename (default: a.out)\n");
    printf("-O level\t\t\tOptimization level: 0=no opt, 1=less, 2=default, 3=aggressive. Size optimisation (-Os, -Oz) TODO\n");
}
