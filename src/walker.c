#include "nus.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nu.h>
#include <comp.h>
#include <lang.h>

extern nu_ast_node_t *g_root_node;

/* Forward declarations */
int eval_expr(nu_ast_node_t *node);
void walk_ast(nu_ast_node_t *node);

static nu_ast_node_t *find_function(nu_ast_node_t *root, const char *name) {
    if (!root) return NULL;
    for (nu_ast_node_t *child = root->first_child; child != NULL; child = child->next_sibling) {
        if (child->type == AST_FUNC_DECL && child->val.str && strcmp(child->val.str, name) == 0) {
            return child;
        }
    }
    return NULL;
}

static int execute_function(nu_ast_node_t *fn_node, nu_ast_node_t *call_node) {
    if (!fn_node) return 0;

    nu_ast_node_t *param_list = NULL;
    nu_ast_node_t *block = NULL;

    for (nu_ast_node_t *child = fn_node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type == AST_PARAM_LIST) param_list = child;
        if (child->type == AST_BLOCK) block = child;
    }

    if (param_list && call_node) {
        nu_ast_node_t *param = param_list->first_child;
        nu_ast_node_t *arg = call_node->first_child;

        while (param && arg) {
            int arg_val = eval_expr(arg);
            const char *param_name = param->val.str;

            if (param_name) {
                symb *sym = symtab_lookup(SymTable, param_name);
                if (!sym) {
                    symtab_add(SymTable, param_name, VAR_INT);
                    sym = symtab_lookup(SymTable, param_name);
                }
                if (sym) {
                    sym->val = arg_val;
                }
            }

            param = param->next_sibling;
            arg = arg->next_sibling;
        }
    }

    if (!block) return 0;

    for (nu_ast_node_t *stmt = block->first_child; stmt != NULL; stmt = stmt->next_sibling) {
        if (stmt->type == AST_RETURN_STMT) {
            nu_ast_node_t *ret_expr = stmt->first_child;
            return ret_expr ? eval_expr(ret_expr) : 0;
        }
        walk_ast(stmt);
    }

    return 0;
}

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

        case AST_FUNC_CALL: {
            nu_ast_node_t *fn = find_function(g_root_node, node->val.str);
            if (!fn) {
                fprintf(stderr, "Runtime Error: Undefined function '%s'\n", node->val.str);
                return 0;
            }
            return execute_function(fn, node);
        }

        default:
            return 0;
    }
}

void walk_ast(nu_ast_node_t *node) {
    if (!node) return;

    switch (node->type) {
        case AST_ROOT: {
	    nu_ast_node_t *main_fn = find_function(node, "main");
            if (main_fn) {
                execute_function(main_fn, NULL);
            } else {
                fprintf(stderr, "Runtime Error: No main() function found\n");
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

                printf("Processed variable declaration: int %s = %d\n", 
                       var_name, result);
            } else {
                printf("Processed variable declaration: int %s (uninitialized)\n", 
                       var_name);
            }
            break;
        }

	case AST_FUNC_DECL: {
            printf("Entering function: %s()\n", 
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
                printf("Processed return statement with evaluated value: %d\n", result);
            } else {
                printf("Processed empty return statement\n");
            }
            break;
        }

        default:
            fprintf(stderr, "Unknown or leaf AST node type: %d\n", node->type);
            break;
    }
}
