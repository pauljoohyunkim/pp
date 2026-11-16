#include "pp_compiler_pipeline.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pp_ast.h"
#include "pp_code_generator.h"
#include "pp_code_optimization.h"
#include "pp_lexer.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"

#define NULL_POINTER_ERROR_HANDLE(ptr)         \
    do {                                       \
        if ((ptr) == NULL) goto error_cleanup; \
    } while (0)

static long get_file_size(const char* filename);
static char* read_file_to_string(const char* filename, long* out_size);

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

static char* read_file_to_string(const char* filename, long* out_size) {
    long size = get_file_size(filename);
    if (size < 0) return NULL;
    FILE* fp = fopen(filename, "r");
    if (!fp) return NULL;
    char* buf = (char*)calloc(size + 1, sizeof(char));
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t read = fread(buf, 1, size, fp);
    buf[read] = '\0';
    fclose(fp);
    if (out_size) *out_size = read;
    return buf;
}

/* Forward declare driver type */

typedef struct {
    PP_Token* tokens;
    size_t count;
} PPIPrelexed;

static int pp_compiler_pipeline_prelex_ppi(const CompilerPipelineOptions* opts, PPIPrelexed* out);
static void pp_compiler_pipeline_prelex_free(PPIPrelexed* pre);

static int pp_compiler_pipeline_compile_source_core_opts(const char* src_path, CompilerPipelineOptions* opts,
                                                         PPIPrelexed* pre, PP_CodeGenerator** out_cg);

/* --- New options-based API --- */
static int pp_compiler_pipeline_prelex_ppi(const CompilerPipelineOptions* opts, PPIPrelexed* out) {
    PP_Lexer* lexer = NULL;
    char* code = NULL;
    PP_Token* tmp = NULL;
    long sz = 0;
    size_t total = 0;

    NULL_POINTER_ERROR_HANDLE(opts);
    NULL_POINTER_ERROR_HANDLE(out);
    out->tokens = NULL;
    out->count = 0;
    if (!opts->ppi_paths || opts->ppi_count == 0) { return 1; }

    for (unsigned int i = 0; i < opts->ppi_count; i++) {
        const char* ppi_path = opts->ppi_paths[i];
        code = read_file_to_string(ppi_path, &sz);
        if (!code) {
            fprintf(stderr, "Failed to read PPI file %s\n", ppi_path);
            goto error_cleanup;
        }
        lexer = pp_lexer_create(code, ppi_path, opts->string_pool);
        free(code);
        code = NULL;
        NULL_POINTER_ERROR_HANDLE(lexer);
        if (!pp_lexer_analyze(lexer)) {
            fprintf(stderr, "Lexer analyze failure for PPI %s\n", ppi_path);
            goto error_cleanup;
        }
        size_t n = lexer->n_tokens;
        if (n > 0 && lexer->tokens[n - 1].token_type == PP_TT_EOF) { n--; }
        if (n > 0) {
            tmp = (PP_Token*)realloc(out->tokens, (total + n) * sizeof(PP_Token));
            NULL_POINTER_ERROR_HANDLE(tmp);
            out->tokens = tmp;
            memcpy(&out->tokens[total], lexer->tokens, n * sizeof(PP_Token));
            total = total + n;
        }
        pp_lexer_free(lexer);
        lexer = NULL;
    }
    out->count = total;
    return 1;

error_cleanup:
    free(code);
    pp_lexer_free(lexer);
    pp_compiler_pipeline_prelex_free(out);
    return 0;
}

static void pp_compiler_pipeline_prelex_free(PPIPrelexed* pre) {
    if (!pre) return;
    if (pre->tokens) {
        free(pre->tokens);
        pre->tokens = NULL;
    }
    pre->count = 0;
}

static int pp_compiler_pipeline_compile_source_core_opts(const char* src_path, CompilerPipelineOptions* opts,
                                                         PPIPrelexed* pre, PP_CodeGenerator** out_cg) {
    char* src_code = NULL;
    PP_Lexer* lexer_src = NULL;
    PP_Parser* parser = NULL;
    PP_SemanticAnalyzer* sa = NULL;
    PP_CodeGenerator* cg = NULL;
    PP_Token* combined = NULL;
    size_t total_errors = 0;
    bool generate_code = true;

    NULL_POINTER_ERROR_HANDLE(src_path);
    NULL_POINTER_ERROR_HANDLE(opts);
    NULL_POINTER_ERROR_HANDLE(out_cg);
    NULL_POINTER_ERROR_HANDLE(opts->string_pool);
    *out_cg = NULL;

    /* Lex source file */
    {
        long sz = 0;
        src_code = read_file_to_string(src_path, &sz);
        if (!src_code) {
            fprintf(stderr, "Could not open %s\n", src_path);
            goto error_cleanup;
        }
        lexer_src = pp_lexer_create(src_code, src_path, opts->string_pool);
        free(src_code);
        src_code = NULL;
        NULL_POINTER_ERROR_HANDLE(lexer_src);
        if (!pp_lexer_analyze(lexer_src)) {
            fprintf(stderr, "Lexer analyze failure\n");
            goto error_cleanup;
        }
        if (lexer_src->error_list && lexer_src->error_list->n_errors > 0) {
            total_errors = total_errors + lexer_src->error_list->n_errors;
            generate_code = false;
            for (size_t i = 0; i < lexer_src->error_list->n_errors; i++) {
                PP_Error* e = &lexer_src->error_list->errors[i];
                fprintf(stderr, "Lexer: %s line %u col %u: %s\n", e->loc.filename ? e->loc.filename : "<unknown>",
                        e->loc.line, e->loc.column, e->message);
            }
        }
        /* Prepend prelexed PPI tokens */
        if (pre && pre->count > 0) {
            size_t src_n = lexer_src->n_tokens;
            size_t new_n = pre->count + src_n;
            combined = (PP_Token*)malloc(new_n * sizeof(PP_Token));
            NULL_POINTER_ERROR_HANDLE(combined);
            memcpy(combined, pre->tokens, pre->count * sizeof(PP_Token));
            memcpy(&combined[pre->count], lexer_src->tokens, src_n * sizeof(PP_Token));
            free(lexer_src->tokens);
            lexer_src->tokens = combined;
            lexer_src->n_tokens = new_n;
            lexer_src->n_tokens_capacity = new_n;
            combined = NULL;
        }
    }

    parser = pp_parser_create(lexer_src, opts->string_pool);
    NULL_POINTER_ERROR_HANDLE(parser);
    if (pp_parser_parse(parser) != 1) { fprintf(stderr, "Parser failed\n"); }
    if (parser->error_list && parser->error_list->n_errors > 0) {
        total_errors = total_errors + parser->error_list->n_errors;
        generate_code = false;
        for (size_t i = 0; i < parser->error_list->n_errors; i++) {
            PP_Error* e = &parser->error_list->errors[i];
            fprintf(stderr, "Parser: %s line %d col %d: %s\n", e->loc.filename ? e->loc.filename : "<unknown>",
                    e->loc.line, e->loc.column, e->message);
        }
    }
    if (opts->verbose && parser->error_list && parser->error_list->n_errors == 0) {
        printf("Parse succeeded. AST:\n");
        pp_ast_root_print(parser->program, 0);
    }

    sa = pp_semantic_analyzer_create(parser, opts->string_pool);
    NULL_POINTER_ERROR_HANDLE(sa);
    if (pp_semantic_analyzer_analyze(sa) != 1) { fprintf(stderr, "Semantic analysis failed\n"); }
    if (sa->error_list && sa->error_list->n_errors > 0) {
        total_errors = total_errors + sa->error_list->n_errors;
        generate_code = false;
        for (size_t i = 0; i < sa->error_list->n_errors; i++) {
            PP_Error* e = &sa->error_list->errors[i];
            fprintf(stderr, "Semantic Analyzer: %s line %d col %d: %s\n",
                    e->loc.filename ? e->loc.filename : "<unknown>", e->loc.line, e->loc.column, e->message);
        }
    }
    if (opts->verbose && sa->error_list && sa->error_list->n_errors == 0) { printf("Semantic analysis succeeded.\n"); }

    if (!generate_code) {
        fprintf(stderr, "Skipping code generation due to %zu errors\n", total_errors);
        goto error_cleanup;
    }

    cg = pp_code_generator_create(sa, opts->string_pool, opts->debug);
    NULL_POINTER_ERROR_HANDLE(cg);
    if (opts->verbose) { printf("Code generator created successfully.\n"); }
    int gen_res = pp_code_generator_generate(cg);
    if (opts->verbose) {
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
    if (opts->verbose) { printf("Code generation succeeded.\n"); }

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
    free(combined);
    *out_cg = NULL;
    return 0;
}

int pp_compiler_pipeline_compile_source_opts(const CompilerPipelineOptions* opts, const char* src_path,
                                             const char* obj_path) {
    PPIPrelexed pre = {0};
    PP_CodeGenerator* cg = NULL;
    int ok = 0;

    if (!opts || !src_path || !obj_path) return 0;
    if (!pp_compiler_pipeline_prelex_ppi(opts, &pre)) goto error_cleanup;
    if (!pp_compiler_pipeline_compile_source_core_opts(src_path, (CompilerPipelineOptions*)opts, &pre, &cg))
        goto error_cleanup;
    if (opts->optimization_mode != '\0' && opts->target_machine != NULL) {
        if (!pp_optimize_module(cg->mod, opts->target_machine, opts->optimization_mode)) {
            fprintf(stderr, "Optimization failed\n");
            goto error_cleanup;
        }
    }
    if (opts->emit_ir) {
        ok = pp_code_generator_emit_ir_file(cg->mod, obj_path);
    } else if (opts->to_assembly) {
        ok = pp_code_generator_emit_assembly_file(cg->mod, obj_path, opts->target_machine);
    } else {
        ok = pp_code_generator_emit_object_file(cg->mod, obj_path, opts->target_machine);
    }
    pp_code_generator_free(cg);
    pp_compiler_pipeline_prelex_free(&pre);
    return ok;

error_cleanup:
    pp_code_generator_free(cg);
    pp_compiler_pipeline_prelex_free(&pre);
    return 0;
}

int pp_compiler_pipeline_compile_source_to_buffer_opts(const CompilerPipelineOptions* opts, const char* src_path,
                                                       LLVMMemoryBufferRef* out_buf) {
    PPIPrelexed pre = {0};
    PP_CodeGenerator* cg = NULL;
    LLVMMemoryBufferRef buf = NULL;

    if (!opts || !src_path || !out_buf) return 0;
    if (!pp_compiler_pipeline_prelex_ppi(opts, &pre)) goto error_cleanup;
    if (!pp_compiler_pipeline_compile_source_core_opts(src_path, (CompilerPipelineOptions*)opts, &pre, &cg))
        goto error_cleanup;
    if (opts->optimization_mode != '\0' && opts->target_machine != NULL) {
        if (!pp_optimize_module(cg->mod, opts->target_machine, opts->optimization_mode)) {
            fprintf(stderr, "Optimization failed\n");
            goto error_cleanup;
        }
    }
    if (!pp_code_generator_emit_object_to_buffer(cg->mod, &buf, opts->target_machine)) goto error_cleanup;
    *out_buf = buf;
    pp_code_generator_free(cg);
    pp_compiler_pipeline_prelex_free(&pre);
    return 1;

error_cleanup:
    pp_code_generator_free(cg);
    pp_compiler_pipeline_prelex_free(&pre);
    return 0;
}
