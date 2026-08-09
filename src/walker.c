#include <nus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nu.h>
#include <comp.h>
#include <lang.h>
#include <vm.h>

extern nu_ast_node_t *g_root_node;
extern nu_mm_t* g_mm;

static BytecodeBuffer *code_buf = NULL;

// Forward decl's
int eval_expr(nu_ast_node_t *node);
void compile_node(nu_ast_node_t *node);
int compile_expr(nu_ast_node_t *node, int target_reg);

static void emit(Instruction inst) {
    if (!code_buf) {
        code_buf = nu_alloc(g_mm, sizeof(BytecodeBuffer));
        code_buf->capacity = 64;
        code_buf->count = 0;
        code_buf->instructions = nu_alloc(g_mm, code_buf->capacity * sizeof(Instruction));
    }
    if (code_buf->count >= code_buf->capacity) {
        code_buf->capacity *= 2;
        code_buf->instructions = nu_realloc(g_mm, code_buf->instructions, code_buf->capacity * sizeof(Instruction));
    }
    code_buf->instructions[code_buf->count++] = inst;
}

static void exec_vsnprintf(const char *fmt, int count, int *args) {
    char buf[512];

    switch (count) {
        case 0: snprintf(buf, sizeof(buf), "%s", fmt); break;
        case 1: snprintf(buf, sizeof(buf), fmt, args[0]); break;
        case 2: snprintf(buf, sizeof(buf), fmt, args[0], args[1]); break;
        case 3: snprintf(buf, sizeof(buf), fmt, args[0], args[1], args[2]); break;
        default: snprintf(buf, sizeof(buf), fmt, args[0], args[1], args[2], args[3]); break;
    }

    printf("%s", buf);
}

static int get_node_value(nu_ast_node_t *node) {
    if (!node) return 0;
    if (node->type == AST_CONST) {
        if (!node->val.str) return 0;
        if (node->val.str[0] == '\'') {
            if (node->val.str[1] == '\\') {
                switch (node->val.str[2]) {
                    case 'n': return '\n';
                    case 't': return '\t';
                    case '0': return '\0';
                    default: return node->val.str[2];
                }
            }
            return (unsigned char)node->val.str[1];
        }
        return atoi(node->val.str);
    }
    if (node->type == AST_IDENT) {
        symb *sym = symtab_lookup(SymTable, node->val.str);
        return sym ? sym->val : 0;
    }
    return 0;
}

int compile_expr(nu_ast_node_t *node, int target_reg) {
    if (!node) return target_reg;

    switch (node->type) {
        case AST_CONST: {
            int val = get_node_value(node);
            emit(LOAD(target_reg, val));
            return target_reg;
        }

        case AST_IDENT: {
            symb *sym = symtab_lookup(SymTable, node->val.str);
            if (sym) {
                emit(LOAD(target_reg, sym->val));
            } else {
                emit(LOAD(target_reg, 0));
            }
            return target_reg;
        }
        
        case AST_ADD: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;

            int r_left = target_reg;
            int r_right = target_reg + 1;
            if (r_right >= NUM_REGS) r_right = NUM_REGS - 1;

            compile_expr(left, r_left);
            compile_expr(right, r_right);

            emit(ADD(target_reg, r_left, r_right));
            return target_reg;
        }

        case AST_SUB: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;

            int r_left = target_reg;
            int r_right = target_reg + 1;
            if (r_right >= NUM_REGS) r_right = NUM_REGS - 1;

            compile_expr(left, r_left);
            compile_expr(right, r_right);

            emit(SUB(target_reg, r_left, r_right));
            return target_reg;
        }

    	  case AST_FUNC_CALL: {
            nu_ast_node_t *fn_node = g_root_node;
            nu_ast_node_t *target_fn = NULL;
            
            for (nu_ast_node_t *child = fn_node->first_child; child != NULL; child = child->next_sibling) {
                if (child->type == AST_FUNC_DECL && child->val.str && strcmp(child->val.str, node->val.str) == 0) {
                    target_fn = child;
                    break;
                }
            }

            if (!target_fn) {
                fprintf(stderr, "Runtime Error: Undefined function '%s'\n", node->val.str);
                emit(LOAD(target_reg, 0));
                return target_reg;
            }

            nu_ast_node_t *param_list = NULL;
            nu_ast_node_t *block = NULL;
            for (nu_ast_node_t *child = target_fn->first_child; child != NULL; child = child->next_sibling) {
                if (child->type == AST_PARAM_LIST) param_list = child;
                if (child->type == AST_BLOCK) block = child;
            }

            nu_ast_node_t *param = param_list ? param_list->first_child : NULL;
            nu_ast_node_t *arg = node->first_child;

            while (param && arg) {
                int arg_val = get_node_value(arg); // or evaluate expression
                const char *param_name = param->val.str;
                if (param_name) {
                    symb *sym = symtab_lookup(SymTable, param_name);
                    if (!sym) {
                        symtab_add(SymTable, param_name, VAR_INT);
                        sym = symtab_lookup(SymTable, param_name);
                    }
                    if (sym) sym->val = arg_val;
                }
                param = param->next_sibling;
                arg = arg->next_sibling;
            }

            if (block) {
                compile_node(block);
            }
            
            if (target_reg != R0) {
                emit(MOV(target_reg, R0));
            }
            return target_reg;
        }

        default:
            return target_reg;
    }
}

void removequotes(const char* in, char* out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (!in) { 
        out[0] = '\0';
        return;
    }

    size_t len = strlen(in);
    if (len >= 2) {
        char first = in[0];
        char last  = in[len - 1];

        if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
            size_t inner = len - 2;
            if (inner >= out_size) inner = out_size - 1;

            memcpy(out, in + 1, inner);
            out[inner] = '\0';
            return;
        }
    }

    strncpy(out, in, out_size - 1);
    out[out_size - 1] = '\0';
}

static void unescape(const char* in, char* out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < out_sz; i++) {
        if (in[i] == '\\') {
            char n = in[i + 1];
            if (n == 'n') { out[j++] = '\n'; i++; continue; }
            if (n == '\\') { out[j++] = '\\'; i++; continue; }
            out[j++] = n;
            i++;
            continue;
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
}


void compile_node(nu_ast_node_t *node) {
    if (!node) return;

    switch (node->type) {
        case AST_ROOT: {
            for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
                compile_node(child);
            }
            break;
        }

        case AST_FUNC_DECL: {
            if (node->val.str && strcmp(node->val.str, "main") == 0) {
                for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
                    compile_node(child);
                }
            }
            break;
        }

        case AST_PRINTF_STMT: {
            nu_ast_node_t *fmt_node = node->first_child;
            if (!fmt_node) break;

            int args[16];
            int count = 0;
            nu_ast_node_t *arg = fmt_node->next_sibling;
            while (arg && count < 16) {
                args[count++] = get_node_value(arg);
                arg = arg->next_sibling;
            }

            size_t in_len = strlen(fmt_node->val.str);
            char *val = nu_alloc(g_mm, in_len + 1);
            if (!val) break;
            
            removequotes(fmt_node->val.str, val, in_len + 1);
            
            char *realfmt = nu_alloc(g_mm, in_len + 1);
            if (!realfmt) { nu_free(g_mm, val); break; }
            
            unescape(val, realfmt, in_len + 1);
            int fmt_id = vm_register_format(realfmt);
            int reg_base = 1;
            for (int i = 0; i < count; i++) {
                emit(LOAD(reg_base + i, args[i]));
            }
            emit(PRINTF(reg_base, count, fmt_id));
            break;
        }

	case AST_PRINT_STMT: {
            nu_ast_node_t *expr = node->first_child;
            if (!expr) break;

            if (expr->type == AST_IDENT) {
                symb *sym = symtab_lookup(SymTable, expr->val.str);
                int val = sym ? sym->val : 0;

                char *str_buf = nu_alloc(g_mm, 32);
                if (!str_buf) break;

                if (val >= 32 && val <= 126) {
                    snprintf(str_buf, 32, "%c", val);
                } else {
                    snprintf(str_buf, 32, "%d", val);
                }

                int str_id = vm_register_string(str_buf);
                emit(LOAD(R0, str_id));
                emit(PRINT(R0));
            } else if (expr->type == AST_CONST && expr->val.str) {
                size_t orig_len = strlen(expr->val.str);
                char *val = nu_alloc(g_mm, orig_len + 1);
                char *realfmt = nu_alloc(g_mm, orig_len + 1);

                if (val && realfmt) {
                    strcpy(val, expr->val.str);

                    // Strip quote layers until none remain
                    while ((val[0] == '"' || val[0] == '\'') && strlen(val) >= 2) {
                        char tmp[512];
                        removequotes(val, tmp, sizeof(tmp));
                        if (strcmp(val, tmp) == 0) break;
                        strcpy(val, tmp);
                    }

                    unescape(val, realfmt, orig_len + 1);

                    int str_id = vm_register_string(realfmt);
                    emit(LOAD(R0, str_id));
                    emit(PRINT(R0));
                }

                if (val) nu_free(g_mm, val);
            }
            break;
        }

        case AST_INT_DECL:
        case AST_CONST_DECL: {
            nu_ast_node_t *var_node = node->first_child;
            nu_ast_node_t *val_node = var_node ? var_node->next_sibling : NULL;
            const char *var_name = (var_node && var_node->val.str) ? var_node->val.str : "?";

            if (val_node) {
                int val = get_node_value(val_node);

                symb *sym = symtab_lookup(SymTable, var_name);
                if (!sym) {
                    symtab_add(SymTable, var_name, VAR_INT);
                    sym = symtab_lookup(SymTable, var_name);
                }
                if (sym) {
                    sym->val = val;
                }
            }
            break;
        }

        case AST_BLOCK: {
            for (nu_ast_node_t *stmt = node->first_child; stmt != NULL; stmt = stmt->next_sibling) {
                compile_node(stmt);
            }
            break;
        }

        case AST_RETURN_STMT: {
            nu_ast_node_t *val_node = node->first_child;
            if (val_node) {
                compile_expr(val_node, R0);
                emit(PRINT(R0));
            }
            break;
        }

        default:
            for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
                compile_node(child);
            }
            break;
    }
}

void walk_ast(nu_ast_node_t *node) {
    if (!node) return;

    // init the bytecode buffer
    code_buf = NULL;

    compile_node(node);

    // append HALT to exit cleanly
    emit(HALT);

    if (code_buf && code_buf->count > 0) {
        run_paw_vm(code_buf->instructions);
        nu_free(g_mm, code_buf->instructions);
        nu_free(g_mm, code_buf);
        code_buf = NULL;
    }
}
