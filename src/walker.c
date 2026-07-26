#include <stdint.h>
#include <stdio.h>
#include <nu.h>
#include <comp.h>
#include <lang.h>

void walk_ast(nu_ast_node_t *node) {
    if (!node) return;

    switch (node->type) {
        case AST_ROOT: {
            /* Top-level program scope: walk each statement sequentially */
            for (nu_ast_node_t *stmt = node->first_child; stmt != NULL; stmt = stmt->next_sibling) {
                walk_ast(stmt);
            }
            break;
        }

        case AST_INT_DECL: {
            /* Child 1: AST_VAR_DECL (variable name)
               Child 2 (optional): AST_CONST or AST_IDENT (initializer) */
            nu_ast_node_t *var_node = node->first_child;
            nu_ast_node_t *val_node = var_node ? var_node->next_sibling : NULL;

            const char *var_name = (var_node && var_node->val.str) ? var_node->val.str : "?";
            if (val_node) {
                printf("[Walker] Processed variable declaration: int %s = %s\n", 
                       var_name, val_node->val.str);
            } else {
                printf("[Walker] Processed variable declaration: int %s (uninitialized)\n", 
                       var_name);
            }
            break;
        }

        case AST_RETURN_STMT: {
            /* Child 1: AST_CONST or AST_IDENT */
            nu_ast_node_t *val_node = node->first_child;

            if (val_node) {
                printf("[Walker] Processed return statement with value: %s\n", 
                       val_node->val.str);
            } else {
                printf("[Walker] Processed empty return statement\n");
            }
            break;
        }

        default:
            printf("[Walker] Unknown or leaf AST node type: %d\n", node->type);
            break;
    }
}
