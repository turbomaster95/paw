#include <stdio.h>
#include <stdlib.h>
#include <comp.h>
#include <etc.h>

extern int yylex(void);
extern FILE *yyin;

YYSTYPE yylval;
char *current_filename = NULL;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    current_filename = argv[1];
    yyin = fopen(current_filename, "r");
    if (!yyin) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    printf("[lob] Tokenizing %s...\n", current_filename);

    parse();

    printf("[lob] End of file reached.\n");

    fclose(yyin);
    return EXIT_SUCCESS;
}
