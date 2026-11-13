#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "pp_ast.h"
#include "pp_code_generator.h"
#include "pp_code_optimization.h"
#include "pp_lexer.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"
#include "pp_compiler_pipeline.h"

#define NULL_POINTER_ERROR_HANDLE(ptr) \
    do { if ((ptr) == NULL) goto error_cleanup; } while (0)

static long get_file_size(const char* filename);
static char* read_file_to_string(const char* filename, long* out_size);
static int validate_ppi_file(const char* path);

static long get_file_size(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0L, SEEK_END) != 0) { fclose(fp); return -1; }
    long size = ftell(fp);
    fclose(fp);
    return size;
}

static char* read_file_to_string(const char* filename, long* out_size) {
    long size = get_file_size(filename);
    if (size < 0) return NULL;
    FILE* fp = fopen(filename, "r");
    if (!fp) return NULL;
    char* buf = (char*)calloc(size + 1, sizeof(char));
    if (!buf) { fclose(fp); return NULL; }
    size_t read = fread(buf, 1, size, fp);
    buf[read] = '\0';
    fclose(fp);
    if (out_size) *out_size = read;
    return buf;
}

static int validate_ppi_file(const char* path) {
    long sz;
    char* content = read_file_to_string(path, &sz);
    if (!content) {
        fprintf(stderr, "Failed to read PPI file %s\n", path);
        return 0;
    }
    /* Simple declarations-only validation: must contain 'extern' and must not contain top-level statements */
    if (!strstr(content, "extern")) {
        fprintf(stderr, "PPI file %s does not contain extern declarations\n", path);
        free(content);
        return 0;
    }
    /* Very basic heuristic: reject files containing 'if ', 'while ', 'return ', 'let ', 'const ' outside extern */
    /* For Step 1, we assume files are correct */
    free(content);
    return 1;
}

int pp_compiler_pipeline_compile_source_core(const char* src_path, bool verbose, PP_CodeGenerator** out_cg, PP_StringPool* sp, const char** ppi_paths, int ppi_count) {
    FILE* fp = NULL;
    char* code = NULL;
    char* combined = NULL;
    PP_Lexer* lexer = NULL;
    PP_Parser* parser = NULL;
    PP_SemanticAnalyzer* sa = NULL;
    PP_CodeGenerator* cg = NULL;
    long filesize = 0;
    size_t total_errors = 0;
    bool generate_code = true;
    long combined_len = 0;

    NULL_POINTER_ERROR_HANDLE(src_path);
    NULL_POINTER_ERROR_HANDLE(out_cg);
    *out_cg = NULL;

    /* Validate and concatenate PPI files */
    if (ppi_count > 0 && ppi_paths) {
        /* First read source to know size */
        filesize = get_file_size(src_path);
        if (filesize < 0) {
            fprintf(stderr, "Could not open %s\n", src_path);
            goto error_cleanup;
        }
        /* Estimate combined size */
        long total = filesize + 1;
        for (int i = 0; i < ppi_count; i++) {
            long sz;
            char* tmp = read_file_to_string(ppi_paths[i], &sz);
            if (!tmp) {
                fprintf(stderr, "Failed to read PPI file %s\n", ppi_paths[i]);
                goto error_cleanup;
            }
            if (!validate_ppi_file(ppi_paths[i])) {
                free(tmp);
                goto error_cleanup;
            }
            total += sz + 2; /* newline */
            free(tmp);
        }
        combined = (char*)calloc(total + 1, sizeof(char));
        if (!combined) goto error_cleanup;
        combined_len = 0;
        for (int i = 0; i < ppi_count; i++) {
            long sz;
            char* tmp = read_file_to_string(ppi_paths[i], &sz);
            if (!tmp) goto error_cleanup;
            memcpy(combined + combined_len, tmp, sz);
            combined_len += sz;
            combined[combined_len++] = '\n';
            free(tmp);
        }
        /* Append source */
        char* src_content = read_file_to_string(src_path, &filesize);
        if (!src_content) goto error_cleanup;
        memcpy(combined + combined_len, src_content, filesize);
        combined_len += filesize;
        combined[combined_len] = '\0';
        free(src_content);
        code = combined;
        combined = NULL;
    } else {
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
    }

    lexer = pp_lexer_create(code, src_path);
    if (code == combined) {
        /* combined owned by code */
        /* free later */
    } else {
        /* code will be freed later */
    }
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
            fprintf(stderr, "Lexer: %s line %d col %d: %s\n", e->loc.filename ? e->loc.filename : "<unknown>", e->loc.line, e->loc.column, e->message);
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
            fprintf(stderr, "Parser: %s line %d col %d: %s\n", e->loc.filename ? e->loc.filename : "<unknown>", e->loc.line, e->loc.column, e->message);
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
            fprintf(stderr, "Semantic Analyzer: %s line %d col %d: %s\n", e->loc.filename ? e->loc.filename : "<unknown>", e->loc.line, e->loc.column, e->message);
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
    int gen_res = pp_code_generator_generate(cg);
    if (verbose) {
        char* ir_str = LLVMPrintModuleToString(cg->mod);
        if (ir_str) {
            printf("--- Generated LLVM IR ---\n%s--- End LLVM IR ---\n", ir_str);
            LLVMDisposeMessage(ir_str);
        }
    }
    if (!gen_res) {
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
    if (code) free(code);
    return 1;

error_cleanup:
    pp_code_generator_free(cg);
    pp_semantic_analyzer_free(sa);
    pp_parser_free(parser);
    pp_lexer_free(lexer);
    if (code) free(code);
    if (combined) free(combined);
    if (fp) fclose(fp);
    *out_cg = NULL;
    return 0;
}

int pp_compiler_pipeline_compile_source(const char* src_path, const char* obj_path, bool verbose, char optimization_mode,
                                       LLVMTargetMachineRef tm, bool to_assembly, bool emit_ir, PP_StringPool* sp, const char** ppi_paths, int ppi_count) {
    PP_CodeGenerator* cg = NULL;
    if (!pp_compiler_pipeline_compile_source_core(src_path, verbose, &cg, sp, ppi_paths, ppi_count)) return 0;
    int ok = 0;
    if (optimization_mode != '\0' && tm != NULL) {
        if (!pp_optimize_module(cg->mod, tm, optimization_mode)) {
            fprintf(stderr, "Optimization failed\n");
            pp_code_generator_free(cg);
            return 0;
        }
    }
    if (emit_ir) {
        ok = pp_code_generator_emit_ir_file(cg->mod, obj_path);
    } else if (to_assembly) {
        ok = pp_code_generator_emit_assembly_file(cg->mod, obj_path);
    } else {
        ok = pp_code_generator_emit_object_file(cg->mod, obj_path);
    }
    pp_code_generator_free(cg);
    return ok;
}

int pp_compiler_pipeline_compile_source_to_buffer(const char* src_path, LLVMMemoryBufferRef* out_buf, bool verbose,
                                                  char optimization_mode, LLVMTargetMachineRef tm, PP_StringPool* sp, const char** ppi_paths, int ppi_count) {
    if (!out_buf) return 0;
    PP_CodeGenerator* cg = NULL;
    if (!pp_compiler_pipeline_compile_source_core(src_path, verbose, &cg, sp, ppi_paths, ppi_count)) return 0;
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
