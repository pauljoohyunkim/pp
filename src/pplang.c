#include <stdio.h>
#include <stdlib.h>
#include "pp_lexer.h"

static long get_file_size(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0L, SEEK_END) != 0) { fclose(fp); return -1; }
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

    PP_Lexer* lexer = pp_lexer_create(code, source_filename);
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
            printf("Lexer: %s line %d col %d: %s\n", e->filename ? e->filename : "<unknown>", e->line, e->column, e->message);
        }
    }

    // Lexical analysis complete – no output for now
    pp_lexer_free(lexer);
    return 0;
}
