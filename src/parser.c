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
        case FUNC:           return "FUNC";
        case IF:             return "IF";
        case INT:            return "INT";
        case RETURN:         return "RETURN";
        case IDENTIFIER:     return "IDENTIFIER";
        case CONSTANT:       return "CONSTANT";
        case STRING_LITERAL: return "STRING_LITERAL";
        default:
            if (token > 0 && token < 256) {
                static char buf[2] = {0};
                buf[0] = (char)token;
                return buf;
            }
            return "UNKNOWN";
    }
}

void synerr(int line, int col, const char* msg) {
    fprintf(stderr, "Syntax Error [%d:%d]: %s", line, col, msg);
    exit(EXIT_FAILURE);
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
        fprintf(stderr, "Syntax Error [Line %d]: %s (got %s: \"%s\")\n", 
                yylineno, msg, tokname(current_tok), yytext);
        char errm[128];
        
        snprintf(errm, sizeof(errm), "%s (got %s: \"%s\")\n", msg, tokname(current_tok), yytext);
        synerr(yylineno, 0, errm);
    }
}

void parse_return_stmt(void) {
    expect(RETURN, "Expected 'return'");

    if (match(CONSTANT) || match(IDENTIFIER)) {
        printf("Parsed return value: %s\n", yytext);
        advance();
    }

    expect(';', "Expected ';' after return value");
}

static void parse_statement(void) {
    switch (current_tok) {
        case RETURN:
            parse_return_stmt();
            break;

        default:
            char errm[128];        
            snprintf(errm, sizeof(errm),
                 "Unexpected token '%s' (\"%s\")\n", tokname(current_tok), yytext);
            synerr(yylineno, 0, errm);
            advance(); /* Skip token to prevent infinite loop */
            break;
    }
}

void parse(void) {
    advance(); /* Prime the token stream */

    while (current_tok != 0) {
        parse_statement();
    }
}
