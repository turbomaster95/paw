#ifndef PAW_COMPILER_H
#define PAW_COMPILER_H

#include <stdbool.h>
#include <vm.h>
#include <nu.h>

typedef enum {
    AST_NUMBER,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_VAR_DECL,
    AST_VAR_REF,
    AST_ASSIGN,
    AST_BLOCK,
    AST_IF,
    AST_WHILE,
    AST_EXPR_STMT,
    AST_RETURN
} ASTNodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType type;
    union {
        int int_value;
        const char *name;
        struct {
            const char *op; /* "+", "-", "*", "/", "==", ">", "<" */
            ASTNode *left;
            ASTNode *right;
        } binary;

        struct {
            const char *op; /* "-" */
            ASTNode *operand;
        } unary;

        struct {
            const char *name;
            ASTNode *initializer;
        } var_decl;

        struct {
            const char *name;
            ASTNode *value;
        } assign;

        struct {
            ASTNode **statements;
            int count;
        } block;

        struct {
            ASTNode *condition;
            ASTNode *then_branch;
            ASTNode *else_branch; /* could.. might.. be NULL */
        } if_stmt;

        struct {
            ASTNode *condition;
            ASTNode *body;
        } while_stmt;

        ASTNode *expr_stmt;
        ASTNode *return_expr;
    };
};

bool compile_ast(nu_ast_node_t *root, Chunk *chunk);

#endif
