#include <nus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <nu.h>
#include <comp.h>
#include <lang.h>

int eval_expr(nu_ast_node_t *node) {
    if (!node) return 0;

    switch (node->type) {
        case AST_CONST:
            return node->val.str ? atoi(node->val.str) : 0;

        case AST_IDENT: {
            symb *sym = symtab_lookup(SymTable, node->val.str);
            return sym ? sym->val : 0;
        }

        case AST_ADD: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;
            return eval_expr(left) + eval_expr(right);
        }

        case AST_SUB: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;
            return eval_expr(left) - eval_expr(right);
        }

        case AST_NEGATIVE: {
            return -eval_expr(node->first_child);
        }

        default:
            return 0;
    }
}

void walk_ast(nu_ast_node_t *node) {
    if (!node) return;

    switch (node->type) {
        case AST_ROOT: {
            for (nu_ast_node_t *stmt = node->first_child; stmt != NULL; stmt = stmt->next_sibling) {
                walk_ast(stmt);
            }
            break;
        }

        case AST_INT_DECL: {
            nu_ast_node_t *var_node = node->first_child;
            nu_ast_node_t *val_node = var_node ? var_node->next_sibling : NULL;

            const char *var_name = (var_node && var_node->val.str) ? var_node->val.str : "?";
            if (val_node) {
                int result = eval_expr(val_node);

                symb *sym = symtab_lookup(SymTable, var_name);
                if (sym) {
                    sym->val = result;
                }

                printf("[Walker] Processed variable declaration: int %s = %d\n", 
                       var_name, result);
            } else {
                printf("[Walker] Processed variable declaration: int %s (uninitialized)\n", 
                       var_name);
            }
            break;
        }

	case AST_FUNC_DECL: {
            printf("[Walker] Entering function: %s()\n", 
                   node->val.str ? node->val.str : "anonymous");
            for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
                walk_ast(child);
            }
            break;
        }

        case AST_PARAM_LIST: {
            break;
        }

        case AST_BLOCK: {
            for (nu_ast_node_t *stmt = node->first_child; stmt != NULL; stmt = stmt->next_sibling) {
                walk_ast(stmt);
            }
            break;
        }

        case AST_RETURN_STMT: {
            nu_ast_node_t *val_node = node->first_child;

            if (val_node) {
                int result = eval_expr(val_node);
                printf("[Walker] Processed return statement with evaluated value: %d\n", result);
            } else {
                printf("[Walker] Processed empty return statement\n");
            }
            break;
        }

        default:
            fprintf(stderr, "Unknown or leaf AST node type: %d\n", node->type);
            break;
    }
}
