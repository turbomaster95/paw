#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vcomp.h>

#define MAX_LOCALS 256

typedef struct {
    const char *name;
    int depth;
} Local;

typedef struct {
    Chunk *chunk;
    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth;
} Compiler;

static void emit_byte(Chunk *chunk, uint8_t byte) {
    chunk_write(chunk, byte);
}

static void emit_bytes(Chunk *chunk, uint8_t byte1, uint8_t byte2) {
    emit_byte(chunk, byte1);
    emit_byte(chunk, byte2);
}

static void emit_constant(Chunk *chunk, Value value) {
    int const_idx = chunk_add_constant(chunk, value);
    if (const_idx > 255) {
        fprintf(stderr, "Too many constants in one bytecode chunk.\n");
        exit(EXIT_FAILURE);
    }
    emit_bytes(chunk, OP_CONSTANT, (uint8_t)const_idx);
}

static int emit_jump(Chunk *chunk, uint8_t instruction) {
    emit_byte(chunk, instruction);
    emit_byte(chunk, 0xff);
    emit_byte(chunk, 0xff);
    return (int)chunk->count - 2;
}

static void patch_jump(Chunk *chunk, int offset) {
    int jump = (int)chunk->count - offset - 2;

    if (jump > 65535) {
        fprintf(stderr, "Code section too large for 16-bit jump offset.\n");
        exit(EXIT_FAILURE);
    }

    chunk->code[offset] = (jump >> 8) & 0xff;
    chunk->code[offset + 1] = jump & 0xff;
}

static void emit_loop(Chunk *chunk, int loop_start) {
    emit_byte(chunk, OP_LOOP);

    int jump = (int)chunk->count - loop_start + 2;
    if (jump > 65535) {
        fprintf(stderr, "Loop body too large.\n");
        exit(EXIT_FAILURE);
    }

    emit_byte(chunk, (jump >> 8) & 0xff);
    emit_byte(chunk, jump & 0xff);
}

static void begin_scope(Compiler *c) {
    c->scope_depth++;
}

static void end_scope(Compiler *c) {
    c->scope_depth--;

    while (c->local_count > 0 && 
           c->locals[c->local_count - 1].depth > c->scope_depth) {
        emit_byte(c->chunk, OP_POP);
        c->local_count--;
    }
}

static int add_local(Compiler *c, const char *name) {
    if (c->local_count >= MAX_LOCALS) {
        fprintf(stderr, "Too many local variables in scope.\n");
        return -1;
    }

    for (int i = c->local_count - 1; i >= 0; i--) {
        Local *local = &c->locals[i];
        if (local->depth < c->scope_depth) break;
        if (strcmp(name, local->name) == 0) {
            fprintf(stderr, "Variable '%s' already declared in this scope.\n", name);
            return -1;
        }
    }

    Local *local = &c->locals[c->local_count];
    local->name = name;
    local->depth = c->scope_depth;
    return c->local_count++;
}

static int resolve_local(Compiler *c, const char *name) {
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (strcmp(name, c->locals[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool compile_node(Compiler *c, ASTNode *node) {
    if (!node) return true;

    switch (node->type) {
        case AST_NUMBER: {
            emit_constant(c->chunk, node->int_value);
            break;
        }

        case AST_UNARY_OP: {
            if (!compile_node(c, node->unary.operand)) return false;

            if (strcmp(node->unary.op, "-") == 0) {
                emit_byte(c->chunk, OP_NEGATE);
            } else {
                fprintf(stderr, "Unknown unary operator: %s\n", node->unary.op);
                return false;
            }
            break;
        }

        case AST_BINARY_OP: {
            if (!compile_node(c, node->binary.left)) return false;
            if (!compile_node(c, node->binary.right)) return false;

            const char *op = node->binary.op;
            if (strcmp(op, "+") == 0)      emit_byte(c->chunk, OP_ADD);
            else if (strcmp(op, "-") == 0) emit_byte(c->chunk, OP_SUB);
            else if (strcmp(op, "*") == 0) emit_byte(c->chunk, OP_MUL);
            else if (strcmp(op, "/") == 0) emit_byte(c->chunk, OP_DIV);
            else if (strcmp(op, "==") == 0) emit_byte(c->chunk, OP_EQUAL);
            else if (strcmp(op, ">") == 0)  emit_byte(c->chunk, OP_GREATER);
            else if (strcmp(op, "<") == 0)  emit_byte(c->chunk, OP_LESS);
            else {
                fprintf(stderr, "Unknown binary operator: %s\n", op);
                return false;
            }
            break;
        }

        case AST_VAR_DECL: {
            if (node->var_decl.initializer) {
                if (!compile_node(c, node->var_decl.initializer)) return false;
            } else {
                emit_constant(c->chunk, 0);
            }

            int slot = add_local(c, node->var_decl.name);
            if (slot == -1) return false;
            break;
        }

        case AST_VAR_REF: {
            int slot = resolve_local(c, node->name);
            if (slot == -1) {
                fprintf(stderr, "Undefined variable '%s'.\n", node->name);
                return false;
            }
            emit_bytes(c->chunk, OP_GET_LOCAL, (uint8_t)slot);
            break;
        }

        case AST_ASSIGN: {
            if (!compile_node(c, node->assign.value)) return false;

            int slot = resolve_local(c, node->assign.name);
            if (slot == -1) {
                fprintf(stderr, "Undefined variable '%s'.\n", node->assign.name);
                return false;
            }
            emit_bytes(c->chunk, OP_SET_LOCAL, (uint8_t)slot);
            break;
        }

        case AST_BLOCK: {
            begin_scope(c);
            for (int i = 0; i < node->block.count; i++) {
                if (!compile_node(c, node->block.statements[i])) return false;
            }
            end_scope(c);
            break;
        }

        case AST_EXPR_STMT: {
            if (!compile_node(c, node->expr_stmt)) return false;
            emit_byte(c->chunk, OP_POP);
            break;
        }

        case AST_IF: {
            if (!compile_node(c, node->if_stmt.condition)) return false;

            int then_jump = emit_jump(c->chunk, OP_JUMP_IF_FALSE);
            emit_byte(c->chunk, OP_POP);

            if (!compile_node(c, node->if_stmt.then_branch)) return false;

            int else_jump = emit_jump(c->chunk, OP_JUMP);

            patch_jump(c->chunk, then_jump);
            emit_byte(c->chunk, OP_POP);

            if (node->if_stmt.else_branch) {
                if (!compile_node(c, node->if_stmt.else_branch)) return false;
            }

            patch_jump(c->chunk, else_jump);
            break;
        }

        case AST_WHILE: {
            int loop_start = (int)c->chunk->count;

            if (!compile_node(c, node->while_stmt.condition)) return false;

            int exit_jump = emit_jump(c->chunk, OP_JUMP_IF_FALSE);
            emit_byte(c->chunk, OP_POP);

            if (!compile_node(c, node->while_stmt.body)) return false;

            emit_loop(c->chunk, loop_start);

            patch_jump(c->chunk, exit_jump);
            emit_byte(c->chunk, OP_POP);
            break;
        }

        case AST_RETURN: {
            if (node->return_expr) {
                if (!compile_node(c, node->return_expr)) return false;
            } else {
                emit_constant(c->chunk, 0);
            }
            emit_byte(c->chunk, OP_RETURN);
            break;
        }
    }

    return true;
}

bool compile_ast(ASTNode *root, Chunk *chunk) {
    Compiler compiler;
    compiler.chunk = chunk;
    compiler.local_count = 0;
    compiler.scope_depth = 0;

    if (!compile_node(&compiler, root)) {
        return false;
    }

    emit_byte(chunk, OP_HALT);
    return true;
}
