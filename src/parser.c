#include <stdio.h>
#include <stdlib.h>
#include <comp.h>
#include <etc.h>

extern int yylex(void);
extern char *yytext;
extern int yylineno;

static int current_tok;

const char *tokname(int token) {
    switch (token) {
        case FUNC:       return "FUNC";
        case IF:         return "IF";
        case INT:        return "INT";
        case RETURN:     return "RETURN";
        case IDENTIFIER: return "IDENTIFIER";
        case CONSTANT:   return "CONSTANT";
        case STRING_LITERAL: return "STRING_LITERAL";
        default:
            /* ASCII single-char tokens like ';', '{', '}', '+', etc. */
            if (token < 256) {
                static char buf[2] = {0};
                buf[0] = (char)token;
                return buf;
            }
            return "UNKNOWN";
    }
}

static void advance(void) {
    current_tok = yylex();
}

static int match(int expected) {
    return current_tok == expected;
}

static void expect(int expected, const char *msg) {
    if (match(expected)) {
        advance();
    } else {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

void parse() {
    int tok;

    while ((tok = yylex()) != 0) {
        printf("Line %3d | Token ID: %-14s | Text: \"%s\"\n", yylineno, tokname(tok), yytext);
    }
}
