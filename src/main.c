#include <stdio.h>
#include <stdlib.h>
#include <comp.h>
#include <etc.h>
#include <nu.h>

extern int yylex(void);
extern FILE *yyin;

/* globals */
nu_mm_t *g_mm = NULL;
nu_ast_t *g_ast = NULL;

YYSTYPE yylval;
char *current_filename = NULL;

char backing[1024 * 1024 * 8]; // 8 mb

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    g_mm = nu_mm_create(NU_MM_SLOB, backing, sizeof(backing));
    g_ast = nu_ast_create(g_mm);
       
    current_filename = argv[1];
    yyin = fopen(current_filename, "r");
    if (!yyin) {
        perror("Error opening file");
        goto fail;
    }

    parse();

    fclose(yyin);
    return EXIT_SUCCESS;

fail:
    nu_ast_destroy(g_ast);
    nu_mm_destroy(g_mm);
    return EXIT_FAILURE;
}
