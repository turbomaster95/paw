#ifndef TAB_H
#define TAB_H

#include <stdint.h>

typedef union {
    int64_t int_val;
    double float_val;
    const char* str_val;
    struct nu_ast_node *node;
} YYSTYPE;

extern YYSTYPE yylval;

enum TokenTypes {
    TOKEN_EOF = 0,

    IDENTIFIER = 1,
    CONSTANT = 2,
    STRING_LITERAL = 3,
    FUNC = 4,
    IF = 5,
    RETURN = 6,
    INT = 7,
};

#endif // TAB_H
