#ifndef COMP_H
#define COMP_H

#include <stdint.h>

#define MAX_BUFFER_SIZE 8192

enum TokenTypes {
    TOKEN_EOF = 0,
    IDENTIFIER,
    CONSTANT,
    STRING_LITERAL,
    FUNC,
    IF,
    RETURN,
    INT,
    CHAR,
    CONST,
    PRINTF,
    PRINT
};

typedef union {
    int64_t int_val;
    double float_val;
    const char* str_val;
    struct nu_ast_node *node;
} YYSTYPE;

typedef enum {
    VAR_START = 0,
    VAR_INT,
    VAR_STRING,
} var_type_t;

typedef enum {
    SCOPE_GLOBAL,
    SCOPE_LOCAL
} VarScope;

struct Symb {
    char *name;
    var_type_t type;
    VarScope scope;
    int location;
    int val;
    struct Symb *next;
};

typedef struct Symb symb;

typedef struct symbol_table {
    symb *head;
} symbt;

extern symbt* SymTable;

symb *symtab_add(symbt *table, const char *name, var_type_t type);

symb *symtab_lookup(symbt *table, const char *name);

extern YYSTYPE yylval;

#endif // COMP_H
