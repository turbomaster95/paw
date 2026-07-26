#ifndef LANG_H
#define LANG_H

#include <stdint.h>

#define AST_TYPE_LIST(X) \
    X(AST_ROOT) \
    X(AST_RETURN_STMT) \
    X(AST_CONST) \
    X(AST_IDENT) \
    X(AST_EXPR_BINARY) \
    X(AST_ASSIGN_STMT) \
    X(AST_INT_DECL) \
    X(AST_VAR_DECL) \
    X(AST_ADD) \
    X(AST_SUB) \
    X(AST_MUL) \
    X(AST_DIV) \
    X(AST_NEGATIVE)

typedef enum {
#define DEFINE_ENUM(name) name,
    AST_TYPE_LIST(DEFINE_ENUM)
#undef DEFINE_ENUM
    AST_TYPE_COUNT
} ast_type_t;

static const char *AST_TYPE_NAMES[] = {
#define DEFINE_STRING(name) #name,
    AST_TYPE_LIST(DEFINE_STRING)
#undef DEFINE_STRING
};

static const char *ast_type_name(uint32_t type) {
    if (type < AST_TYPE_COUNT) {
        return AST_TYPE_NAMES[type];
    }
    return "AST_UNKNOWN";
}

#endif
