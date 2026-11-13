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



int pp_compiler_pipeline_compile_source_core(const char* src_path, bool verbose, PP_CodeGenerator** out_cg, PP_StringPool* sp, const char** ppi_paths, int ppi_count) {
    char* src_code = NULL;
    PP_Lexer* lexer_src = NULL;
    PP_Lexer* lexer_ppi = NULL;
    PP_Parser* parser = NULL;
    PP_SemanticAnalyzer* sa = NULL;
    PP_CodeGenerator* cg = NULL;
    PP_Token* ppi_tokens = NULL;
    PP_Token* combined = NULL;
    size_t ppi_tokens_count = 0;
    size_t total_errors = 0;
    bool generate_code = true;

    NULL_POINTER_ERROR_HANDLE(src_path);
    NULL_POINTER_ERROR_HANDLE(out_cg);
    NULL_POINTER_ERROR_HANDLE(sp);
    *out_cg = NULL;

    /* Lex PPI files individually and collect tokens */
    if (ppi_count > 0 && ppi_paths) {
        for (int i = 0; i < ppi_count; i++) {
            const char* ppi_path = ppi_paths[i];
            long sz = 0;
            char* code = read_file_to_string(ppi_path, &sz);
            if (!code) {
                fprintf(stderr, "Failed to read PPI file %s\n", ppi_path);
                goto error_cleanup;
            }
            lexer_ppi = pp_lexer_create(code, ppi_path, sp);
            free(code);
            NULL_POINTER_ERROR_HANDLE(lexer_ppi);
            if (!pp_lexer_analyze(lexer_ppi)) {
                fprintf(stderr, "Lexer analyze failure for PPI %s\n", ppi_path);
                pp_lexer_free(lexer_ppi);
                lexer_ppi = NULL;
                goto error_cleanup;
            }
            if (lexer_ppi->error_list && lexer_ppi->error_list->n_errors > 0) {
                total_errors += lexer_ppi->error_list->n_errors;
                generate_code = false;
                for (size_t e = 0; e < lexer_ppi->error_list->n_errors; e++) {
                    PP_Error* err = &lexer_ppi->error_list->errors[e];
                    fprintf(stderr, "Lexer PPI: %s line %u col %u: %s\n",
                        err->loc.filename ? err->loc.filename : "<unknown>",
                        err->loc.line, err->loc.column, err->message);
                }
            }
            size_t n_tokens = lexer_ppi->n_tokens;
            /* Skip trailing EOF for PPI */
            if (n_tokens > 0 && lexer_ppi->tokens[n_tokens - 1].token_type == PP_TT_EOF) {
                n_tokens--;
            }
            if (n_tokens > 0) {
                PP_Token* tmp = (PP_Token*)realloc(ppi_tokens, (ppi_tokens_count + n_tokens) * sizeof(PP_Token));
                if (!tmp) {
                    pp_lexer_free(lexer_ppi);
                    lexer_ppi = NULL;
                    goto error_cleanup;
                }
                ppi_tokens = tmp;
                memcpy(&ppi_tokens[ppi_tokens_count], lexer_ppi->tokens, n_tokens * sizeof(PP_Token));
                ppi_tokens_count += n_tokens;
            }
            pp_lexer_free(lexer_ppi);
            lexer_ppi = NULL;
        }
    }

    /* Lex source file */
    {
        long sz = 0;
        src_code = read_file_to_string(src_path, &sz);
        if (!src_code) {
            fprintf(stderr, "Could not open %s\n", src_path);
            goto error_cleanup;
        }
        lexer_src = pp_lexer_create(src_code, src_path, sp);
        free(src_code);
        src_code = NULL;
        NULL_POINTER_ERROR_HANDLE(lexer_src);
        if (!pp_lexer_analyze(lexer_src)) {
            fprintf(stderr, "Lexer analyze failure\n");
            goto error_cleanup;
        }
        if (lexer_src->error_list && lexer_src->error_list->n_errors > 0) {
            total_errors += lexer_src->error_list->n_errors;
            generate_code = false;
            for (size_t i = 0; i < lexer_src->error_list->n_errors; i++) {
                PP_Error* e = &lexer_src->error_list->errors[i];
                fprintf(stderr, "Lexer: %s line %u col %u: %s\n", e->loc.filename ? e->loc.filename : "<unknown>", e->loc.line, e->loc.column, e->message);
            }
        }
        /* Prepend PPI tokens */
        if (ppi_tokens_count > 0) {
            size_t src_n = lexer_src->n_tokens;
            size_t new_n = ppi_tokens_count + src_n;
            combined = (PP_Token*)malloc(new_n * sizeof(PP_Token));
            if (!combined) {
                goto error_cleanup;
            }
            memcpy(combined, ppi_tokens, ppi_tokens_count * sizeof(PP_Token));
            memcpy(&combined[ppi_tokens_count], lexer_src->tokens, src_n * sizeof(PP_Token));
            free(lexer_src->tokens);
            lexer_src->tokens = combined;
            lexer_src->n_tokens = new_n;
            /* Ensure capacity large enough for parser */
            lexer_src->n_tokens_capacity = new_n;
        }
        free(ppi_tokens);
        ppi_tokens = NULL;
    }

    parser = pp_parser_create(lexer_src, sp);
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

    sa = pp_semantic_analyzer_create(parser, sp);
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

    cg = pp_code_generator_create(sa, sp);
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
    pp_lexer_free(lexer_src);
    lexer_src = NULL;
    return 1;

error_cleanup:
    pp_code_generator_free(cg);
    pp_semantic_analyzer_free(sa);
    pp_parser_free(parser);
    pp_lexer_free(lexer_src);
    if (ppi_tokens) free(ppi_tokens);
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
        ok = pp_code_generator_emit_assembly_file(cg->mod, obj_path, tm);
    } else {
        ok = pp_code_generator_emit_object_file(cg->mod, obj_path, tm);
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
    if (!pp_code_generator_emit_object_to_buffer(cg->mod, &buf, tm)) {
        pp_code_generator_free(cg);
        return 0;
    }
    *out_buf = buf;
    pp_code_generator_free(cg);
    return 1;
}
