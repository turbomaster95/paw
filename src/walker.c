#include <nus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nu.h>
#include <comp.h>
#include <lang.h>
#include <vm.h>

#define NEED_FORMAT
#include <common.h>

extern nu_ast_node_t *g_root_node;
extern nu_mm_t* g_mm;

static BytecodeBuffer *code_buf = NULL;
static bool in_function = false;
static int current_local_reg = 4;

// Forward decl's
int eval_expr(nu_ast_node_t *node);
void compile_node(nu_ast_node_t *node);
int compile_expr(nu_ast_node_t *node, int target_reg);
void removequotes(const char* in, char* out, size_t out_size);
void unescape(const char* in, char* out, size_t out_sz);

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

static int get_node_value(nu_ast_node_t *node) {
    if (!node) return 0;

    if (node->type == AST_CONST) {
        if (!node->val.str) return 0;

        const char *src = node->val.str;
        char clean[512];
        removequotes(src, clean, sizeof(clean));

        if (src[0] == '\'') {
            return (unsigned char)clean[0];
        }

        return atoi(clean);
    }

    if (node->type == AST_IDENT) {
        symb *sym = symtab_lookup(SymTable, node->val.str);
        if (sym && sym->scope == SCOPE_GLOBAL) {
            return sym->val;
        }
    }

    if (node->type == AST_ADD) {
        nu_ast_node_t *left = node->first_child;
        nu_ast_node_t *right = left ? left->next_sibling : NULL;
        return get_node_value(left) + get_node_value(right);
    }

    if (node->type == AST_SUB) {
        nu_ast_node_t *left = node->first_child;
        nu_ast_node_t *right = left ? left->next_sibling : NULL;
        return get_node_value(left) - get_node_value(right);
    }

    return 0;
}

int compile_expr(nu_ast_node_t *node, int target_reg) {
    if (!node) return target_reg;

    switch (node->type) {
        case AST_CONST: {
            int val = get_node_value(node);
            emit(EMIT_LOAD(target_reg, val));
            return target_reg;
        }

        case AST_IDENT: {
            symb *sym = symtab_lookup(SymTable, node->val.str);
            if (!sym) {
                fprintf(stderr, "Error: Undefined variable '%s'\n", node->val.str);
                emit(EMIT_LOAD(target_reg, 0));
                return target_reg;
            }

            if (sym->scope == SCOPE_GLOBAL) {
                emit(EMIT_LOAD(target_reg, sym->val));
            } else {
                emit(EMIT_MOV(target_reg, sym->location));
            }
            return target_reg;
        }

        case AST_ASSIGN_STMT: {
            nu_ast_node_t *var_node = node->first_child;
            nu_ast_node_t *val_node = var_node ? var_node->next_sibling : NULL;

            if (!var_node || !val_node) return target_reg;

            const char *var_name = var_node->val.str;
            if (!var_name && var_node->first_child) {
                var_name = var_node->first_child->val.str;
            }

            symb *sym = symtab_lookup(SymTable, var_name);
            if (!sym) {
                fprintf(stderr, "Error: Undefined variable '%s'\n", var_name ? var_name : "?");
                return target_reg;
            }

            if (sym->scope == SCOPE_GLOBAL) {
                compile_expr(val_node, target_reg);
                sym->val = get_node_value(val_node);
                emit(EMIT_MOV(sym->location, target_reg));
            } else {
                compile_expr(val_node, sym->location);
                if (target_reg != sym->location) {
                    emit(EMIT_MOV(target_reg, sym->location));
                }
            }
            return target_reg;
        }

        case AST_NEGATIVE: {
            nu_ast_node_t *operand = node->first_child;
            if (operand) {
                int operand_reg = current_local_reg++;
                compile_expr(operand, operand_reg);
                emit(EMIT_LOAD(target_reg, 0));
                emit(EMIT_SUB(target_reg, target_reg, operand_reg));
                current_local_reg--;
            }
            break;
        }

        case AST_ADD:
        case AST_SUB:
        case AST_MUL:
        case AST_DIV:
        case AST_MOD:
        case AST_BAND:
        case AST_BOR:
        case AST_BXOR:
        case AST_SHL:
        case AST_SHR: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;
            if (!left || !right) break;

            int left_reg = current_local_reg++;
            compile_expr(left, left_reg);

            int right_reg = current_local_reg++;
            compile_expr(right, right_reg);

            emit(EMIT_MOV(target_reg, left_reg));
            
            switch (node->type) {
                case AST_ADD:  emit(EMIT_ADD(target_reg, target_reg, right_reg)); break;
                case AST_SUB:  emit(EMIT_SUB(target_reg, target_reg, right_reg)); break;
                case AST_MUL:  emit(EMIT_MUL(target_reg, target_reg, right_reg)); break;
                case AST_DIV:  emit(EMIT_DIV(target_reg, target_reg, right_reg)); break;
                case AST_MOD:  emit(EMIT_MOD(target_reg, target_reg, right_reg)); break;
                case AST_BAND: emit(EMIT_BAND(target_reg, target_reg, right_reg)); break;
                case AST_BOR:  emit(EMIT_BOR(target_reg, target_reg, right_reg)); break;
                case AST_BXOR: emit(EMIT_BXOR(target_reg, target_reg, right_reg)); break;
                case AST_SHL:  emit(EMIT_SHL(target_reg, target_reg, right_reg)); break;
                case AST_SHR:  emit(EMIT_SHR(target_reg, target_reg, right_reg)); break;
                default: break;
            }

            current_local_reg--;
            break;
        }

        case AST_BNOT: {
            nu_ast_node_t *operand = node->first_child;
            if (operand) {
                compile_expr(operand, target_reg);
                emit(EMIT_BNOT(target_reg, target_reg));
            }
            break;
        }

        case AST_LNOT: {
            nu_ast_node_t *operand = node->first_child;
            if (operand) {
                int operand_reg = current_local_reg++;
                compile_expr(operand, operand_reg);
        
                emit(EMIT_LOAD(target_reg, 0));
                emit(EMIT_CMPI(operand_reg, 0));
                // Replace with your VM's conditional set/jump logic for logical NOT
                current_local_reg--;
            }
            break;
        }

        case AST_FUNC_CALL: {
            nu_ast_node_t *target_fn = NULL;
            
            for (nu_ast_node_t *child = g_root_node->first_child; child != NULL; child = child->next_sibling) {
                if (child->type == AST_FUNC_DECL && child->val.str && strcmp(child->val.str, node->val.str) == 0) {
                    target_fn = child;
                    break;
                }
            }

            if (!target_fn) {
                fprintf(stderr, "Runtime Error: Undefined function '%s'\n", node->val.str);
                emit(EMIT_LOAD(target_reg, 0));
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
                const char *param_name = NULL;
                for (nu_ast_node_t *pchild = param->first_child; pchild != NULL; pchild = pchild->next_sibling) {
                    if (pchild->type == AST_PARAM_NAME) {
                        param_name = pchild->val.str;
                        break;
                    }
                }
                
                if (param_name) {
                    symb *sym = symtab_lookup(SymTable, param_name);
                    if (!sym) {
                        symtab_add(SymTable, param_name, VAR_INT);
                        sym = symtab_lookup(SymTable, param_name);
                    }
                    sym->scope = SCOPE_LOCAL;
                    sym->location = current_local_reg++;
                    compile_expr(arg, sym->location);
                }
                param = param->next_sibling;
                arg = arg->next_sibling;
            }

            if (block) {
                compile_node(block);
            }
            
            if (target_reg != R0) {
                emit(EMIT_MOV(target_reg, R0));
            }
            return target_reg;
        }

        default:
            return target_reg;
    }
    return (int)-1;
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

void unescape(const char* in, char* out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < out_sz; i++) {
        if (in[i] == '\\') {
            char n = in[i + 1];
            if (n == '\0') {
                break;
            }
            switch (n) {
                case 'n':  out[j++] = '\n'; i++; break;
                case 't':  out[j++] = '\t'; i++; break;
                case 'r':  out[j++] = '\r'; i++; break;
                case '0':  out[j++] = '\0'; i++; break;
                case 'a':  out[j++] = '\a'; i++; break;
                case 'b':  out[j++] = '\b'; i++; break;
                case 'f':  out[j++] = '\f'; i++; break;
                case 'v':  out[j++] = '\v'; i++; break;
                case '\\': out[j++] = '\\'; i++; break;
                case '"':  out[j++] = '"';  break;
                case '\'': out[j++] = '\''; break;
                default:
                    out[j++] = n;
                    i++;
                    break;
            }
        } else {
            out[j++] = in[i];
        }
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
            bool prev_in_func = in_function;
            in_function = true;
            for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
                if (child->type == AST_BLOCK) {
                    compile_node(child);
                }
            }
            in_function = prev_in_func;	    
            break;
        }

        case AST_FUNC_CALL: {
            compile_expr(node, R0);
            break;
        }

        case AST_PRINTF_STMT: {
            nu_ast_node_t *fmt_node = node->first_child;
            if (!fmt_node) break;

            size_t in_len = strlen(fmt_node->val.str);
            char *val = nu_alloc(g_mm, in_len + 1);
            if (!val) break;
            
            removequotes(fmt_node->val.str, val, in_len + 1);
            
            char *realfmt = nu_alloc(g_mm, in_len + 1);
            if (!realfmt) { nu_free(g_mm, val); break; }
            
            unescape(val, realfmt, in_len + 1);
            int fmt_id = vm_register_format(realfmt);

            emit(EMIT_LOAD(R0, fmt_id));

            int count = 0;
            int reg_base = 1;
            nu_ast_node_t *arg = fmt_node->next_sibling;
            while (arg && count < UF_REGS) {
                compile_expr(arg, reg_base + count);
                count++;
                arg = arg->next_sibling;
            }

            emit(INST_SYS(2));
            break;
        }

        case AST_PRINT_STMT: {
            nu_ast_node_t *expr = node->first_child;
            if (!expr) break;

            if (expr->type == AST_CONST && expr->val.str && expr->val.str[0] == '"') {
                size_t orig_len = strlen(expr->val.str);
                char *val = nu_alloc(g_mm, orig_len + 1);
                char *realfmt = nu_alloc(g_mm, orig_len + 1);

                if (val && realfmt) {
                    strcpy(val, expr->val.str);

                    while ((val[0] == '"' || val[0] == '\'') && strlen(val) >= 2) {
                        char tmp[512];
                        removequotes(val, tmp, sizeof(tmp));
                        if (strcmp(val, tmp) == 0) break;
                        strcpy(val, tmp);
                    }

                    unescape(val, realfmt, orig_len + 1);

                    int str_id = vm_register_string(realfmt);
                    emit(EMIT_LOAD(R0, str_id));
                    emit(INST_SYS(1));
                }

                if (val) nu_free(g_mm, val);
                if (realfmt) nu_free(g_mm, realfmt);
            } else {
                const char *fmt_str = "%d\n";

                if (expr->type == AST_IDENT) {
                    symb *sym = symtab_lookup(SymTable, expr->val.str);
                    if (sym && sym->type == VAR_CHAR) {
                        fmt_str = "%c\n";
                    }
                } else if (expr->type == AST_CONST && expr->val.str && expr->val.str[0] == '\'') {
                    fmt_str = "%c\n";
                }

                int fmt_id = vm_register_format(fmt_str);
                emit(EMIT_LOAD(R0, fmt_id));
                compile_expr(expr, R1);
                emit(INST_SYS(2));
            }
            break;
        }

        case AST_CHAR_DECL:
        case AST_INT_DECL:
        case AST_CONST_DECL: {
            nu_ast_node_t *var_node = node->first_child;
            nu_ast_node_t *val_node = var_node ? var_node->next_sibling : NULL;
            const char *var_name = (var_node && var_node->val.str) ? var_node->val.str : "?";

            symb *sym = symtab_lookup(SymTable, var_name);
            if (!sym) {
                symtab_add(SymTable, var_name, node->type == AST_CHAR_DECL ? VAR_CHAR : VAR_INT);
                sym = symtab_lookup(SymTable, var_name);
            }

    	    if (in_function) {
                sym->scope = SCOPE_LOCAL;
                sym->location = current_local_reg++;

                if (val_node) {
                    compile_expr(val_node, sym->location);
                } else {
                    emit(EMIT_LOAD(sym->location, 0));
                }
            } else {
                sym->scope = SCOPE_GLOBAL;
                if (val_node) {
                    sym->val = get_node_value(val_node);
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
            }
            break;
        }

        case AST_ASSIGN_STMT: {
            nu_ast_node_t *var_node = node->first_child;
            nu_ast_node_t *val_node = var_node ? var_node->next_sibling : NULL;

            if (!var_node || !val_node) break;

            const char *var_name = var_node->val.str;
            if (!var_name && var_node->first_child) {
                var_name = var_node->first_child->val.str;
            }

            if (!var_name) {
                fprintf(stderr, "Error: Invalid assignment target\n");
                break;
            }

            symb *sym = symtab_lookup(SymTable, var_name);
            if (!sym) {
                fprintf(stderr, "Error: Undefined variable '%s' in assignment\n", var_name);
                break;
            }

            if (sym->scope == SCOPE_GLOBAL) {
                compile_expr(val_node, R1);
                sym->val = get_node_value(val_node); 
            } else {
                compile_expr(val_node, sym->location);
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

bool write_bytecode_file(const char *filename, const BytecodeBuffer *buf) {
    if (!buf || !filename) return false;

    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open output bytecode file");
        return false;
    }

    uint32_t str_count = vm_get_string_count();

    uint32_t string_table_bytes = sizeof(uint32_t);
    for (uint32_t i = 0; i < str_count; i++) {
        const char *str = vm_get_string(i);
        uint32_t len = str ? (uint32_t)strlen(str) : 0;
        string_table_bytes += sizeof(uint32_t) + len;
    }

    VMHeader hdr = {
        .magic = VM_MAGIC,
        .version = VM_VERSION,
        .inst_count = (uint32_t)buf->count,
        .data_size = 0
    };

    if (fwrite(&hdr, sizeof(VMHeader), 1, f) != 1) {
        fclose(f);
        return false;
    }

    fwrite(&str_count, sizeof(uint32_t), 1, f);
    for (uint32_t i = 0; i < str_count; i++) {
        const char *str = vm_get_string(i);
        uint32_t len = str ? (uint32_t)strlen(str) : 0;
        fwrite(&len, sizeof(uint32_t), 1, f);
        if (len > 0) {
            fwrite(str, sizeof(char), len, f);
        }
    }

    fwrite(buf->instructions, sizeof(Instruction), buf->count, f);

    fclose(f);
    return true;
}

void walk_ast_to_file(nu_ast_node_t *node, const char *out_filename) {
    if (!node) return;

    g_root_node = node;
    code_buf = NULL;
    in_function = false;
    current_local_reg = 4;

    nu_ast_node_t *main_fn = NULL;
    for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type == AST_FUNC_DECL && child->val.str && strcmp(child->val.str, "main") == 0) {
            main_fn = child;
            break;
        }
    }

    if (main_fn) {
        for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
            if (child->type == AST_CONST_DECL || child->type == AST_INT_DECL || child->type == AST_CHAR_DECL) {
                compile_node(child);
            }
        }
        compile_node(main_fn);
    } else {
        for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
            compile_node(child);
        }
    }

    emit(EMIT_HALT(R0));

    if (code_buf && code_buf->count > 0) {
        write_bytecode_file(out_filename, code_buf);
        nu_free(g_mm, code_buf->instructions);
        nu_free(g_mm, code_buf);
        code_buf = NULL;
    }
}

