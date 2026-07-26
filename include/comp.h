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

    IDENTIFIER,
    CONSTANT,
    STRING_LITERAL,
    FUNC,
    IF,
    RETURN,
    INT,
};

#endif // TAB_H
