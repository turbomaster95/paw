#include <stdio.h>
#include <stdlib.h>
#include <comp.h>
#include <etc.h>
#include <nu.h>
#include <glog.h>

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
        glog_log(NULL, 0, 0, GLOG_INFO, "Usage: %s <source_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    g_mm = nu_mm_create(NU_MM_ARENA, backing, sizeof(backing));
    g_ast = nu_ast_create(g_mm);

    glog_init();
    glog_config.show_source = true;
    glog_config.use_color = 1;
    glog_config.prefix = "paw";

    current_filename = argv[1];
    yyin = fopen(current_filename, "r");
    if (!yyin) {
        glog_log(current_filename, 0, 0, GLOG_FATAL, "Error Opening file!");
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
