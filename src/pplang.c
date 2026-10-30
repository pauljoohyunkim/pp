#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pp_ast.h"
#include "pp_code_generator.h"
#include "pp_lexer.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"
#include "pp_linker_offload.h"

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

typedef enum {
    PP_UNKNOWN,
    PP_SOURCE,
    PP_OBJECT
} PPLang_Filetype;

typedef struct {
    char* src_path;
    char* obj_path;
    PPLang_Filetype type;
} PPLang_Input;

typedef struct {
    PPLang_Input* inputs;
    unsigned int count;
    unsigned int capacity;
} PPLang_InputList;

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
            if (list->inputs[i].obj_path) free(list->inputs[i].obj_path);
        }
        free(list->inputs);
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
        if (strcmp(dot, ".o") == 0 || strcmp(dot, ".obj") == 0 || strcmp(dot, ".a") == 0 || strcmp(dot, ".so") == 0 || strcmp(dot, ".dylib") == 0) {
            return PP_OBJECT;
        }
    }

    fp = fopen(path, "rb");
    if (!fp) return PP_UNKNOWN;

    r = fread(magic, 1, 4, fp);
    if (fp) fclose(fp);
    if (r >= 4) {
        // ELF magic
        if (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
            return PP_OBJECT;
        }
        // Mach-O 64-bit little/big endian
        if ((magic[0] == 0xfe && magic[1] == 0xed && magic[2] == 0xfa && magic[3] == 0xed) ||
            (magic[0] == 0xce && magic[1] == 0xfa && magic[2] == 0xed && magic[3] == 0xfe)) {
            return PP_OBJECT;
        }
        // PE/COFF DOS stub magic "MZ"
        if (magic[0] == 0x4d && magic[1] == 0x5a) {
            return PP_OBJECT;
        }
    }
    return PP_SOURCE;
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
    printf("Usage: pplang [-h] [-v] [-c] [-o filename] src.pp\n");
    printf("-h\t\t\t\tShow this help menu\n");
    printf("-v\t\t\t\tShow verbose output\n");
    printf("-c\t\t\t\tCompile to object file.\n");
    printf("-o filename\t\tOutput filename (default: a.out)\n");
}

int main(int argc, char** argv) {
    int exit_code = 0;

    bool to_object = false;
    bool verbose = false;
    char output_filename[BUFSIZ] = { 0 };
    char temp_filename[L_tmpnam] = { 0 };
    strcpy(output_filename, "a.out");
    int opt;
    while ((opt = getopt(argc, argv, "hcvo:")) != -1) {
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
                strncpy(output_filename, optarg, BUFSIZ-1);
                break;
            default:
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "No source file given.\n");
        return 1;
    }

    const char* source_filename = argv[optind];
    long filesize = get_file_size(source_filename);
    if (filesize < 0) {
        fprintf(stderr, "Could not open %s\n", source_filename);
        return 1;
    }
    

    FILE* fp = fopen(source_filename, "r");
    if (!fp) {
        fprintf(stderr, "Could not open %s\n", source_filename);
        return 1;
    }

    char* code = (char*)calloc(filesize + 1, sizeof(char));
    if (!code) {
        fprintf(stderr, "Could not allocate memory for code.\n");
        fclose(fp);
        return 1;
    }

    fread(code, sizeof(char), filesize, fp);
    fclose(fp);

    PP_Lexer* lexer = NULL;
    PP_Parser* parser = NULL;
    PP_SemanticAnalyzer* sa = NULL;
    PP_CodeGenerator* cg = NULL;
    bool generate_code = true;
    size_t total_errors = 0;

    lexer = pp_lexer_create(code, source_filename);
    free(code);
    if (!lexer) {
        fprintf(stderr, "Lexer creation failure\n");
        return 1;
    }

    if (!pp_lexer_analyze(lexer)) {
        fprintf(stderr, "Lexer analyze failure\n");
        pp_lexer_free(lexer);
        return 1;
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

#ifdef STOP_AT_LEXER
    printf("STOP_AT_LEXER: skipping parser and semantic analyzer\n");
    goto error_cleanup_exit;
#endif

    parser = pp_parser_create(lexer);
    if (!parser) {
        fprintf(stderr, "Parser creation failure\n");
        exit_code = 1;
        goto error_cleanup_exit;
    }

    int ret = pp_parser_parse(parser);
    if (ret != 1) { fprintf(stderr, "Parser failed\n"); }

    if (parser->error_list && parser->error_list->n_errors > 0) {
        total_errors += parser->error_list->n_errors;
        generate_code = false;
        for (size_t i = 0; i < parser->error_list->n_errors; i++) {
            PP_Error* e = &parser->error_list->errors[i];
            fprintf(stderr, "Parser: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>", e->line,
                    e->column, e->message);
        }
    } else {
        if (verbose) {
            printf("Parse succeeded. AST:\n");
            pp_ast_root_print(parser->program, 0);
        }
        
    }

#ifdef STOP_AT_PARSER
    printf("STOP_AT_PARSER: skipping semantic analyzer\n");
    goto error_cleanup_exit;
#endif

    sa = pp_semantic_analyzer_create(parser);
    if (!sa) {
        fprintf(stderr, "Semantic analyzer creation failure\n");
        exit_code = 1;
        goto error_cleanup_exit;
    }

    ret = pp_semantic_analyzer_analyze(sa);
    if (ret != 1) { fprintf(stderr, "Semantic analysis failed\n"); }

    if (sa->error_list && sa->error_list->n_errors > 0) {
        total_errors += sa->error_list->n_errors;
        generate_code = false;
        for (size_t i = 0; i < sa->error_list->n_errors; i++) {
            PP_Error* e = &sa->error_list->errors[i];
            fprintf(stderr, "Semantic Analyzer: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>",
                    e->line, e->column, e->message);
        }
    } else {
        if (verbose) {
            printf("Semantic analysis succeeded.\n");
            printf("Type Table:\n");
            pp_global_type_table_print(sa->gtt);
        } 
    }

#ifdef STOP_AT_SEMANTIC_ANALYZER
    printf("STOP_AT_SEMANTIC_ANALYZER: skipping code generator\n");
    generate_code = false;
#endif

    if (!generate_code) {
        fprintf(stderr, "Skipping code generation due to %zu errors\n", total_errors);
        exit_code = (total_errors > 0) ? 1 : 0;
        goto error_cleanup_exit;
    }

    cg = pp_code_generator_create(sa);
    if (!cg) {
        fprintf(stderr, "Code generator creation failure\n");
        exit_code = 1;
        goto error_cleanup_exit;
    } else {
        if (verbose) {
           printf("Code generator created successfully.\n"); 
        } 
    }

    ret = pp_code_generator_generate(cg);
    if (!ret) {
        fprintf(stderr, "Code generation failure\n");
        exit_code = 1;
        goto error_cleanup_exit;
    } else {
        if (verbose) {
            printf("Code generation succeeded.\n");
            pp_environment_backend_print_type_map(cg->global_env_b);
            pp_environment_backend_print_value_map(cg->global_env_b);
            LLVMDumpModule(cg->mod);
        }
        if (to_object) {
            if (!pp_code_generator_emit_object_file(cg->mod, output_filename)) {
            fprintf(stderr, "Could not write file to %s. Do you have write permission?\n", output_filename);
            exit_code = 1;
            goto error_cleanup_exit;
        }
        } else {
            // Need to create temporary object file, then invoke the linker
            if (!tmpnam(temp_filename)) {
                fprintf(stderr, "Could not acquire temporary file name for temporary object file. Do you have write permission?\n");
                exit_code = 1;
                goto error_cleanup_exit;
            }

            if (verbose) {
                printf("Before linking, a temporary object file will be compiled to %s.\n", temp_filename);
            }
            
            if (!pp_code_generator_emit_object_file(cg->mod, temp_filename)) {
                fprintf(stderr, "Could not write file for temporary object file to %s. Do you have write permission?\n", temp_filename);
                exit_code = 1;
            goto error_cleanup_exit;
            }

            if (verbose) {
                printf("Invoking linker...\n");
            }

            if (!pp_linker_offload(temp_filename, output_filename)) {
               fprintf(stderr, "Could not link to %s. Do you have write permission?\n", output_filename);
            exit_code = 1;
            goto error_cleanup_exit; 
            }
            if (verbose) {
                printf("Output %s\n", output_filename);
            }
        }
    }

error_cleanup_exit:
    if (cg) pp_code_generator_free(cg);
    if (sa) pp_semantic_analyzer_free(sa);
    if (parser) pp_parser_free(parser);
    if (lexer) pp_lexer_free(lexer);
    return exit_code;
}
