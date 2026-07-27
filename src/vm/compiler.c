#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AST_VAR_DECL VCOMP_AST_VAR_DECL
#define AST_BLOCK    VCOMP_AST_BLOCK
#include <vcomp.h>
#undef AST_VAR_DECL
#undef AST_BLOCK

#include <nu.h>
#include <lang.h>

#define MAX_LOCALS 256
#define MAX_FUNCTIONS 64
#define MAX_FIXUPS 128

typedef struct {
    const char *name;
    int depth;
} Local;

typedef struct {
    const char *name;
    int address;
    int arity;
} FunctionSymbol;

typedef struct {
    int const_index;
    const char *func_name;
} CallFixup;

typedef struct {
    Chunk *chunk;
    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;

    FunctionSymbol functions[MAX_FUNCTIONS];
    int function_count;

    CallFixup fixups[MAX_FIXUPS];
    int fixup_count;
} Compiler;

static void emit_byte(Chunk *chunk, uint8_t byte) {
    chunk_write(chunk, byte);
}

static void emit_bytes(Chunk *chunk, uint8_t byte1, uint8_t byte2) {
    emit_byte(chunk, byte1);
    emit_byte(chunk, byte2);
}

static int emit_constant(Chunk *chunk, Value value) {
    int const_idx = chunk_add_constant(chunk, value);
    if (const_idx > 255) {
        fprintf(stderr, "Too many constants in chunk.\n");
        exit(EXIT_FAILURE);
    }
    emit_bytes(chunk, OP_CONSTANT, (uint8_t)const_idx);
    return const_idx;
}

static int emit_jump(Chunk *chunk, uint8_t instruction) {
    emit_byte(chunk, instruction);
    emit_byte(chunk, 0xff);
    emit_byte(chunk, 0xff);
    return (int)chunk->count;
}

static void patch_jump(Chunk *chunk, int offset) {
    int jump = (int)chunk->count - offset;

    if (jump > 65535) {
        fprintf(stderr, "Jump offset exceeds 16-bit limit.\n");
        exit(EXIT_FAILURE);
    }

    chunk->code[offset - 2] = (jump >> 8) & 0xff;
    chunk->code[offset - 1] = jump & 0xff;
}

static void begin_scope(Compiler *c) {
    c->scope_depth++;
}

static void end_scope_no_pop(Compiler *c) {
    while (c->local_count > 0 && 
           c->locals[c->local_count - 1].depth >= c->scope_depth) {
        c->local_count--;
    }
    c->scope_depth--;
}

static int add_local(Compiler *c, const char *name) {
    if (!name) return -1;

    if (c->local_count >= MAX_LOCALS) {
        fprintf(stderr, "Too many local variables in scope.\n");
        return -1;
    }

    for (int i = c->local_count - 1; i >= 0; i--) {
        Local *local = &c->locals[i];
        if (local->depth < c->scope_depth) break;
        if (strcmp(name, local->name) == 0) {
            fprintf(stderr, "Variable '%s' already declared in scope.\n", name);
            return -1;
        }
    }

    Local *local = &c->locals[c->local_count];
    local->name = name;
    local->depth = c->scope_depth;
    return c->local_count++;
}

static int resolve_local(Compiler *c, const char *name) {
    if (!name) return -1;
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (strcmp(name, c->locals[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static void register_function(Compiler *c, const char *name, int address, int arity) {
    if (!name) return;
    for (int i = 0; i < c->function_count; i++) {
        if (strcmp(c->functions[i].name, name) == 0) {
            c->functions[i].address = address;
            c->functions[i].arity = arity;
            return;
        }
    }
    if (c->function_count >= MAX_FUNCTIONS) {
        fprintf(stderr, "Too many function declarations.\n");
        return;
    }
    c->functions[c->function_count].name = name;
    c->functions[c->function_count].address = address;
    c->functions[c->function_count].arity = arity;
    c->function_count++;
}

static int resolve_function(Compiler *c, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < c->function_count; i++) {
        if (strcmp(c->functions[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void add_call_fixup(Compiler *c, int const_idx, const char *func_name) {
    if (c->fixup_count >= MAX_FIXUPS) {
        fprintf(stderr, "Too many function call fixups.\n");
        exit(EXIT_FAILURE);
    }
    c->fixups[c->fixup_count].const_index = const_idx;
    c->fixups[c->fixup_count].func_name = func_name;
    c->fixup_count++;
}

static bool apply_fixups(Compiler *c) {
    for (int i = 0; i < c->fixup_count; i++) {
        int func_idx = resolve_function(c, c->fixups[i].func_name);
        if (func_idx == -1 || c->functions[func_idx].address == -1) {
            fprintf(stderr, "Undefined function '%s'.\n", c->fixups[i].func_name ? c->fixups[i].func_name : "null");
            return false;
        }
        c->chunk->constants[c->fixups[i].const_index] = (Value)c->functions[func_idx].address;
    }
    return true;
}

static bool compile_node(Compiler *c, nu_ast_node_t *node) {
    if (!node) return true;

    switch (node->type) {
        case AST_ROOT: {
            for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
                if (!compile_node(c, child)) return false;
            }

            int main_idx = resolve_function(c, "main");
            if (main_idx != -1) {
                if (c->functions[main_idx].address != -1) {
                    emit_constant(c->chunk, (Value)c->functions[main_idx].address);
                } else {
                    int const_idx = emit_constant(c->chunk, 0);
                    add_call_fixup(c, const_idx, "main");
                }
                emit_bytes(c->chunk, OP_CALL, 0);
            }
            break;
        }

        case AST_FUNC_DECL: {
            int jump_over = emit_jump(c->chunk, OP_JUMP);
            int func_address = (int)c->chunk->count;

	    int prev_local_count = c->local_count;
	    c->local_count = 0;

            begin_scope(c);

            int arity = 0;
            nu_ast_node_t *param_list = node->first_child;
            if (param_list && param_list->type == AST_PARAM_LIST) {
                for (nu_ast_node_t *param = param_list->first_child; param != NULL; param = param->next_sibling) {
                    if (param->type == AST_PARAM && param->val.str) {
                        add_local(c, param->val.str);
                        arity++;
                    }
                }
            }

            register_function(c, node->val.str, func_address, arity);

            nu_ast_node_t *body = param_list ? param_list->next_sibling : NULL;
            if (body) {
                if (!compile_node(c, body)) return false;
            }

            end_scope_no_pop(c);
	    c->local_count = prev_local_count;
            patch_jump(c->chunk, jump_over);
            break;
        }

        case AST_FUNC_CALL: {
            uint8_t arg_count = 0;
            for (nu_ast_node_t *arg = node->first_child; arg != NULL; arg = arg->next_sibling) {
                if (!compile_node(c, arg)) return false;
                arg_count++;
            }

            const char *func_name = node->val.str;
            int func_idx = resolve_function(c, func_name);

            if (func_idx != -1 && c->functions[func_idx].address != -1) {
                emit_constant(c->chunk, (Value)c->functions[func_idx].address);
            } else {
                int const_idx = emit_constant(c->chunk, 0);
                add_call_fixup(c, const_idx, func_name);
            }

            emit_bytes(c->chunk, OP_CALL, arg_count);
            break;
        }

        case AST_INT_DECL: {
            nu_ast_node_t *var_decl = node->first_child;
            if (!var_decl) return false;

            nu_ast_node_t *init_expr = var_decl->next_sibling ? var_decl->next_sibling : var_decl->first_child;
            if (init_expr) {
                if (!compile_node(c, init_expr)) return false;
            } else {
                emit_constant(c->chunk, 0);
            }

            int slot = add_local(c, var_decl->val.str);
            if (slot == -1) return false;
            break;
        }

        case AST_VAR_DECL: {
            nu_ast_node_t *init_expr = node->first_child;
            if (init_expr) {
                if (!compile_node(c, init_expr)) return false;
            } else {
                emit_constant(c->chunk, 0);
            }

            int slot = add_local(c, node->val.str);
            if (slot == -1) return false;
            break;
        }

        case AST_CONST: {
            Value val = node->val.str ? (Value)atoi(node->val.str) : (Value)node->val.i64;
            emit_constant(c->chunk, val);
            break;
        }

        case AST_IDENT: {
            const char *var_name = node->val.str;
            int slot = resolve_local(c, var_name);
            if (slot == -1) {
                fprintf(stderr, "Undefined variable '%s'.\n", var_name ? var_name : "null");
                return false;
            }
            emit_bytes(c->chunk, OP_GET_LOCAL, (uint8_t)slot);
            break;
        }

        case AST_ADD: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;
            if (!left || !right) return false;

            if (!compile_node(c, left)) return false;
            if (!compile_node(c, right)) return false;

            emit_byte(c->chunk, OP_ADD);
            break;
        }

        case AST_SUB: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;
            if (!left || !right) return false;

            if (!compile_node(c, left)) return false;
            if (!compile_node(c, right)) return false;

            emit_byte(c->chunk, OP_SUB);
            break;
        }

        case AST_MUL: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;
            if (!left || !right) return false;

            if (!compile_node(c, left)) return false;
            if (!compile_node(c, right)) return false;

            emit_byte(c->chunk, OP_MUL);
            break;
        }

        case AST_DIV: {
            nu_ast_node_t *left = node->first_child;
            nu_ast_node_t *right = left ? left->next_sibling : NULL;
            if (!left || !right) return false;

            if (!compile_node(c, left)) return false;
            if (!compile_node(c, right)) return false;

            emit_byte(c->chunk, OP_DIV);
            break;
        }

        case AST_ASSIGN_STMT: {
            nu_ast_node_t *val_expr = node->first_child;
            if (!val_expr || !compile_node(c, val_expr)) return false;

            const char *var_name = node->val.str;
            int slot = resolve_local(c, var_name);
            if (slot == -1) {
                fprintf(stderr, "Undefined variable '%s'.\n", var_name ? var_name : "null");
                return false;
            }
            emit_bytes(c->chunk, OP_SET_LOCAL, (uint8_t)slot);
            break;
        }

        case AST_BLOCK: {
            begin_scope(c);
            for (nu_ast_node_t *stmt = node->first_child; stmt != NULL; stmt = stmt->next_sibling) {
                if (!compile_node(c, stmt)) return false;
            }
            end_scope_no_pop(c);
            break;
        }

        case AST_RETURN_STMT: {
            nu_ast_node_t *ret_expr = node->first_child;
            if (ret_expr) {
                if (!compile_node(c, ret_expr)) return false;
            } else {
                emit_constant(c->chunk, 0);
            }
            emit_byte(c->chunk, OP_RETURN);
            break;
        }

        default:
            fprintf(stderr, "Unhandled AST node type: %u (%s)\n", node->type, ast_type_name(node->type));
            return false;
    }

    return true;
}

bool compile_ast(nu_ast_node_t *root, Chunk *chunk) {
    Compiler compiler;
    compiler.chunk = chunk;
    compiler.local_count = 0;
    compiler.scope_depth = 0;
    compiler.function_count = 0;
    compiler.fixup_count = 0;

    for (int i = 0; i < MAX_FUNCTIONS; i++) {
        compiler.functions[i].address = -1;
    }

    if (!compile_node(&compiler, root)) {
        return false;
    }

    if (!apply_fixups(&compiler)) {
        return false;
    }

    emit_byte(chunk, OP_HALT);
    return true;
}
