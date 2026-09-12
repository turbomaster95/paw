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
    PRINT,
    RARROW, // ->
    LARROW,  // <-
    EXTERN,
    LIB
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
    VAR_CHAR
} var_type_t;

typedef enum {
    SCOPE_GLOBAL,
    SCOPE_LOCAL
} VarScope;

typedef enum {
    FFI_TYPE_VOID = 0,
    FFI_TYPE_INT,
    FFI_TYPE_CHAR,
    FFI_TYPE_CSTRING,
    FFI_TYPE_POINTER
} ffi_type_t;

#define FFI_MAX_ARGS 16

typedef struct {
    ffi_type_t return_type;
    ffi_type_t args[FFI_MAX_ARGS];
    uint32_t arg_count;
    int variadic;
} ffi_signature_t;

struct Symb {
    char *name;

    var_type_t type;

    VarScope scope;

    int location;

    int val;

    int is_ffi;

    char *ffi_library;
    char *ffi_symbol;

    ffi_signature_t ffi_signature;

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
