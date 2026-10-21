#include <stdio.h>
#include <stdlib.h>

#include "pp_ast.h"
#include "pp_lexer.h"
#include "pp_parser.h"
#include "pp_semantic_analyzer.h"

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

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: pplang src.pp\n");
        return 1;
    }

    const char* source_filename = argv[1];
    long filesize = get_file_size(source_filename);
    if (filesize < 0) {
        printf("Could not open %s\n", source_filename);
        return 1;
    }

    FILE* fp = fopen(source_filename, "r");
    if (!fp) {
        printf("Could not open %s\n", source_filename);
        return 1;
    }

    char* code = (char*)calloc(filesize + 1, sizeof(char));
    if (!code) {
        printf("Could not allocate memory for code.\n");
        fclose(fp);
        return 1;
    }

    fread(code, sizeof(char), filesize, fp);
    fclose(fp);

    PP_Lexer* lexer = NULL;
    PP_Parser* parser = NULL;
    PP_SemanticAnalyzer* sa = NULL;

    lexer = pp_lexer_create(code, source_filename);
    free(code);
    if (!lexer) {
        printf("Lexer creation failure\n");
        return 1;
    }

    if (!pp_lexer_analyze(lexer)) {
        printf("Lexer analyze failure\n");
        pp_lexer_free(lexer);
        return 1;
    }

    if (lexer->error_list && lexer->error_list->n_errors > 0) {
        for (size_t i = 0; i < lexer->error_list->n_errors; i++) {
            PP_Error* e = &lexer->error_list->errors[i];
            printf("Lexer: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>", e->line, e->column,
                   e->message);
        }
    }

#ifdef STOP_AT_LEXER
    printf("STOP_AT_LEXER: skipping parser and semantic analyzer\n");
    goto error_cleanup_exit;
#endif

    parser = pp_parser_create(lexer);
    if (!parser) {
        printf("Parser creation failure\n");
        pp_lexer_free(lexer);
        return 1;
    }

    int ret = pp_parser_parse(parser);
    if (ret != 1) { printf("Parser failed\n"); }

    if (parser->error_list && parser->error_list->n_errors > 0) {
        for (size_t i = 0; i < parser->error_list->n_errors; i++) {
            PP_Error* e = &parser->error_list->errors[i];
            printf("Parser: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>", e->line, e->column,
                   e->message);
        }
    } else {
        printf("Parse succeeded. AST:\n");
        pp_ast_root_print(parser->program, 0);
    }

#ifdef STOP_AT_PARSER
    printf("STOP_AT_PARSER: skipping semantic analyzer\n");
    goto error_cleanup_exit;
#endif

    sa = pp_semantic_analyzer_create(parser);
    if (!sa) {
        printf("Semantic analyzer creation failure\n");
        pp_parser_free(parser);
        pp_lexer_free(lexer);
        return 1;
    }

    ret = pp_semantic_analyzer_analyze(sa);
    if (ret != 1) { printf("Semantic analysis failed\n"); }

    if (sa->error_list && sa->error_list->n_errors > 0) {
        for (size_t i = 0; i < sa->error_list->n_errors; i++) {
            PP_Error* e = &sa->error_list->errors[i];
            printf("Semantic Analyzer: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>", e->line,
                   e->column, e->message);
        }
    } else {
        printf("Semantic analysis succeeded.\n");
        printf("Type Table:\n");
        pp_global_type_table_print(sa->gtt);
    }

error_cleanup_exit:
    if (sa) pp_semantic_analyzer_free(sa);
    if (parser) pp_parser_free(parser);
    if (lexer) pp_lexer_free(lexer);
    return 0;
}
