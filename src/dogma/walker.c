#include <nus.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nu.h>
#include <comp.h>
#include <lang.h>
#include <vm.h>
#include <pawffi.h>

#define NEED_FORMAT
#include <common.h>

#define FP R15
#define WORD_SIZE 4
#define GLOBAL_DATA_BASE 4

extern nu_ast_node_t *g_root_node;
extern nu_mm_t *g_mm;

static BytecodeBuffer *code_buf = NULL;
static bool in_function = false;
static int current_frame_bytes = 0;
static size_t global_data_offset = GLOBAL_DATA_BASE;

#define FFI_TYPE_UNKNOWN 7

int eval_expr(nu_ast_node_t *node);
void compile_node(nu_ast_node_t *node);
int compile_expr(nu_ast_node_t *node, int target_reg);

static void removequotes(const char *in, char *out, size_t out_size);
static void unescape(const char *in, char *out, size_t out_sz);

static int alloc_stack_offset(int size_bytes) {
    int aligned_size = (size_bytes + (WORD_SIZE - 1)) & ~(WORD_SIZE - 1);

    if (aligned_size <= 0) {
        return 0;
    }

    if (global_data_offset + global_data_offset > RAM_SIZE) {
        return 0;
    }

    if ((size_t)current_frame_bytes + (size_t)aligned_size + global_data_offset >= RAM_SIZE - 4) {
        fprintf(stderr, _("Error: stack frame is too large\n"));
        return 0;
    }

    current_frame_bytes += aligned_size;
    return -current_frame_bytes;
}

static int alloc_global_storage(size_t size) {
    size_t aligned = (global_data_offset + 3u) & ~3u;

    if (size == 0) {
        size = 4;
    }

    if (aligned > RAM_SIZE - 4 || size > RAM_SIZE - 4 - aligned) {
        fprintf(stderr, _("Error: global data area is too large\n"));
        return -1;
    }

    if (aligned + size >= RAM_SIZE - 4) {
        fprintf(stderr, _("Error: global data overlaps stack\n"));
        return -1;
    }

    global_data_offset = aligned + size;
    return (int)aligned;
}

static int global_fp_offset(int address) {
    return address - (RAM_SIZE - 4);
}

static void emit(Instruction inst) {
    if (!code_buf) {
        code_buf = nu_alloc(g_mm, sizeof(BytecodeBuffer));

        if (!code_buf) {
            return;
        }

        code_buf->capacity = 64;
        code_buf->count = 0;
        code_buf->instructions = nu_alloc(
            g_mm,
            code_buf->capacity * sizeof(Instruction)
        );

        if (!code_buf->instructions) {
            nu_free(g_mm, code_buf);
            code_buf = NULL;
            return;
        }
    }

    if (code_buf->count >= code_buf->capacity) {
        code_buf->capacity *= 2;

        code_buf->instructions = nu_realloc(
            g_mm,
            code_buf->instructions,
            code_buf->capacity * sizeof(Instruction)
        );

        if (!code_buf->instructions) {
            code_buf = NULL;
            return;
        }
    }

    code_buf->instructions[code_buf->count++] = inst;
}

static int get_node_value(nu_ast_node_t *node) {
    if (!node) return 0;

    switch (node->type) {
        case AST_CONST: {
            if (!node->val.str) return 0;

            char clean[512];

            removequotes(node->val.str, clean, sizeof(clean));

            if (node->val.str[0] == '\'') {
                return (unsigned char)clean[0];
            }

            return atoi(clean);
        }

        case AST_IDENT: {
            symb *sym = symtab_lookup(SymTable, node->val.str);

            if (sym && sym->scope == SCOPE_GLOBAL && !sym->is_array) {
                return sym->val;
            }

            return 0;
        }

        case AST_NEGATIVE:
            return -get_node_value(node->first_child);

        case AST_BNOT:
            return ~get_node_value(node->first_child);

        case AST_LNOT:
            return !get_node_value(node->first_child);

        case AST_ADD: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;

            return get_node_value(l) + get_node_value(r);
        }

        case AST_SUB: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;

            return get_node_value(l) - get_node_value(r);
        }

        case AST_MUL: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;

            return get_node_value(l) * get_node_value(r);
        }

        case AST_DIV: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;
            int den = get_node_value(r);

            return den != 0 ? get_node_value(l) / den : 0;
        }

        case AST_MOD: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;
            int den = get_node_value(r);

            return den != 0 ? get_node_value(l) % den : 0;
        }

        case AST_BAND: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;

            return get_node_value(l) & get_node_value(r);
        }

        case AST_BOR: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;

            return get_node_value(l) | get_node_value(r);
        }

        case AST_BXOR: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;

            return get_node_value(l) ^ get_node_value(r);
        }

        case AST_SHL: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;

            return get_node_value(l) << get_node_value(r);
        }

        case AST_SHR: {
            nu_ast_node_t *l = node->first_child;
            nu_ast_node_t *r = l ? l->next_sibling : NULL;

            return get_node_value(l) >> get_node_value(r);
        }

        default:
            return 0;
    }
}

static var_type_t expr_var_type(nu_ast_node_t *node) {
    if (!node) return VAR_UNKNOWN;

    switch (node->type) {
        case AST_CONST:
            if (!node->val.str) return VAR_UNKNOWN;
            if (node->val.str[0] == '\'') return VAR_CHAR;
            if (node->val.str[0] == '"') return VAR_STRING;
            return VAR_INT;

        case AST_IDENT: {
            symb *sym = symtab_lookup(SymTable, node->val.str);
            return sym ? sym->type : VAR_UNKNOWN;
        }

        case AST_ARRAY_INDEX:
            return expr_var_type(node->first_child);

        case AST_CAST:
            if (node->val.str) {
                if (strcmp(node->val.str, "char") == 0) return VAR_CHAR;
                if (strcmp(node->val.str, "int") == 0) return VAR_INT;
            }

            return expr_var_type(node->first_child);

        default:
            if (node->first_child) {
                return expr_var_type(node->first_child);
            }

            return VAR_UNKNOWN;
    }
}

const char *ffi_library_for_alias(const char *alias) {
    if (!alias || !g_root_node) return NULL;

    for (nu_ast_node_t *child = g_root_node->first_child;
         child != NULL;
         child = child->next_sibling) {
        if (child->type != AST_EXTERN_DECL) continue;
        if (!child->val.str) continue;
        if (strcmp(child->val.str, alias) != 0) continue;

        nu_ast_node_t *lib = child->first_child;

        if (lib && lib->type == AST_LIB_DECL && lib->val.str) {
            return lib->val.str;
        }
    }

    return NULL;
}

static int register_runtime_string(const char *str) {
    if (!str) return -1;
    return vm_register_string(str);
}

static int compile_ffi_argument(nu_ast_node_t *node, int target_reg) {
    if (!node) {
        emit(EMIT_LOAD(target_reg, 0));
        return target_reg;
    }

    if (node->type == AST_CONST &&
        node->val.str &&
        node->val.str[0] == '"') {
        char clean[1024];
        char real[1024];

        removequotes(node->val.str, clean, sizeof(clean));
        unescape(clean, real, sizeof(real));

        int string_id = register_runtime_string(real);

        if (string_id < 0) {
            fprintf(stderr, _("FFI: failed to register string argument\n"));
            emit(EMIT_LOAD(target_reg, 0));
            return target_reg;
        }

        emit(EMIT_LOAD(target_reg, string_id));
        return target_reg;
    }

    return compile_expr(node, target_reg);
}

static int ffi_expr_type(nu_ast_node_t *node) {
    if (!node) return FFI_TYPE_UNKNOWN;

    switch (node->type) {
        case AST_CONST:
            if (!node->val.str) return FFI_TYPE_UNKNOWN;
            if (node->val.str[0] == '"') return PAW_FFI_CSTRING;
            if (node->val.str[0] == '\'') return PAW_FFI_CHAR;
            return PAW_FFI_INT;

        case AST_IDENT: {
            symb *sym = symtab_lookup(SymTable, node->val.str);

            if (!sym) return FFI_TYPE_UNKNOWN;
            if (sym->type == VAR_CHAR) return PAW_FFI_CHAR;

            return PAW_FFI_INT;
        }

        case AST_ARRAY_INDEX:
            return expr_var_type(node) == VAR_CHAR ? PAW_FFI_CHAR : PAW_FFI_INT;

        case AST_NEGATIVE:
        case AST_ADD:
        case AST_SUB:
        case AST_MUL:
        case AST_DIV:
        case AST_MOD:
        case AST_BAND:
        case AST_BOR:
        case AST_BXOR:
        case AST_SHL:
        case AST_SHR:
        case AST_BNOT:
        case AST_LNOT:
            return PAW_FFI_INT;

        default:
            return FFI_TYPE_UNKNOWN;
    }
}

static uint64_t ffi_pack_types(nu_ast_node_t *first_arg, uint32_t *argc_out) {
    uint64_t packed = 0;
    uint32_t argc = 0;

    for (nu_ast_node_t *arg = first_arg;
         arg != NULL && argc < 15;
         arg = arg->next_sibling) {
        uint64_t type = (uint64_t)(ffi_expr_type(arg) & 0x7);

        packed |= type << (argc * 3);
        argc++;
    }

    if (argc_out) {
        *argc_out = argc;
    }

    return packed;
}

static int find_array_symbol(nu_ast_node_t *node, symb **sym_out) {
    if (!node || node->type != AST_ARRAY_INDEX) {
        return -1;
    }

    nu_ast_node_t *array_node = node->first_child;

    if (!array_node || array_node->type != AST_IDENT || !array_node->val.str) {
        fprintf(stderr, _("Error: array expression must name an array\n"));
        return -1;
    }

    symb *sym = symtab_lookup(SymTable, array_node->val.str);

    if (!sym) {
        fprintf(
            stderr,
            _("Error: Undefined array '%s'\n"),
            array_node->val.str
        );
        return -1;
    }

    if (!sym->is_array) {
        fprintf(
            stderr,
            _("Error: '%s' is not an array\n"),
            array_node->val.str
        );
        return -1;
    }

    if (sym_out) {
        *sym_out = sym;
    }

    return 0;
}

static void emit_array_address(nu_ast_node_t *node, symb *sym, int target_reg, int index_reg) {
    nu_ast_node_t *index = node->first_child
        ? node->first_child->next_sibling
        : NULL;

    if (!index) {
        emit(EMIT_LOAD(target_reg, 0));
        return;
    }

    compile_expr(index, index_reg);

    if (sym->elem_size == 4) {
        emit(EMIT_SHL(index_reg, index_reg, 2));
    }

    emit(EMIT_MOV(target_reg, FP));

    int base_offset;

    if (sym->scope == SCOPE_GLOBAL) {
        base_offset = global_fp_offset(sym->location);
    } else {
        base_offset = sym->location;
    }

    if (base_offset != 0) {
        emit(EMIT_ADDI(target_reg, target_reg, base_offset));
    }

    emit(EMIT_ADD(target_reg, target_reg, index_reg));
}

static void emit_array_store(symb *sym, int value_reg, int address_reg) {
    if (sym->elem_size == 1) {
        emit(EMIT_STOREB_MEM(value_reg, address_reg, 0));
    } else {
        emit(EMIT_STORE_MEM(value_reg, address_reg, 0));
    }
}

static void emit_array_store_at(symb *sym, int value_reg, int base_reg, int offset) {
    if (sym->elem_size == 1) {
        emit(EMIT_STOREB_MEM(value_reg, base_reg, offset));
    } else {
        emit(EMIT_STORE_MEM(value_reg, base_reg, offset));
    }
}

static void emit_array_zero(symb *sym, int base_reg, int offset) {
    emit(EMIT_LOAD(R1, 0));
    emit_array_store_at(sym, R1, base_reg, offset);
}

static nu_ast_node_t *array_size_node(nu_ast_node_t *decl) {
    for (nu_ast_node_t *child = decl->first_child;
         child != NULL;
         child = child->next_sibling) {
        if (child->type == AST_ARRAY_SIZE) {
            return child;
        }
    }

    return NULL;
}

static nu_ast_node_t *array_init_first_node(nu_ast_node_t *decl) {
    nu_ast_node_t *size_node = array_size_node(decl);

    if (!size_node) {
        return NULL;
    }

    return size_node->next_sibling;
}

static int compile_array_decl(nu_ast_node_t *node, symb *sym) {
    nu_ast_node_t *size_node = array_size_node(node);

    if (!size_node) {
        return -1;
    }

    size_t array_size = (size_t)size_node->val.i64;

    if (sym->array_size != array_size) {
        sym->array_size = array_size;
    }

    if (sym->elem_size == 0) {
        sym->elem_size = (sym->type == VAR_CHAR) ? 1 : 4;
    }

    if (array_size == 0 ||
        array_size > SIZE_MAX / sym->elem_size) {
        fprintf(stderr, _("Error: array is too large\n"));
        return -1;
    }

    size_t total_size = array_size * sym->elem_size;

    if (in_function) {
        if (!sym->storage_allocated) {
            if (total_size > INT32_MAX) {
                fprintf(stderr, _("Error: local array is too large\n"));
                return -1;
            }

            sym->location = alloc_stack_offset((int)total_size);

            if (sym->location == 0) {
                return -1;
            }

            sym->storage_allocated = 1;
        }

        sym->scope = SCOPE_LOCAL;

        for (size_t i = 0; i < array_size; i++) {
            int offset = sym->location + (int)(i * sym->elem_size);
            emit_array_zero(sym, FP, offset);
        }

        nu_ast_node_t *init = array_init_first_node(node);
        size_t index = 0;

        while (init && index < array_size) {
            compile_expr(init, R1);
            emit(EMIT_PUSH(R1));

            emit(EMIT_MOV(R2, FP));

            int offset = sym->location + (int)(index * sym->elem_size);

            if (offset != 0) {
                emit(EMIT_ADDI(R2, R2, offset));
            }

            emit(EMIT_POP(R1));
            emit_array_store(sym, R1, R2);

            init = init->next_sibling;
            index++;
        }

        return 0;
    }

    sym->scope = SCOPE_GLOBAL;

    if (!sym->storage_allocated) {
        int location = alloc_global_storage(total_size);

        if (location < 0) {
            return -1;
        }

        sym->location = location;
        sym->storage_allocated = 1;
    }

    for (size_t i = 0; i < array_size; i++) {
        int offset = global_fp_offset(sym->location) +
                     (int)(i * sym->elem_size);

        emit_array_zero(sym, FP, offset);
    }

    nu_ast_node_t *init = array_init_first_node(node);
    size_t index = 0;

    while (init && index < array_size) {
        compile_expr(init, R1);
        emit(EMIT_PUSH(R1));

        emit(EMIT_MOV(R2, FP));

        int offset = global_fp_offset(sym->location) +
                     (int)(index * sym->elem_size);

        if (offset != 0) {
            emit(EMIT_ADDI(R2, R2, offset));
        }

        emit(EMIT_POP(R1));
        emit_array_store(sym, R1, R2);

        init = init->next_sibling;
        index++;
    }

    return 0;
}

static int compile_assignment_node(nu_ast_node_t *node, int target_reg) {
    nu_ast_node_t *var_node = node ? node->first_child : NULL;
    nu_ast_node_t *val_node = var_node
        ? var_node->next_sibling
        : NULL;

    if (!var_node || !val_node) {
        return target_reg;
    }

    if (var_node->type == AST_ARRAY_INDEX) {
        symb *sym = NULL;

        if (find_array_symbol(var_node, &sym) != 0) {
            return target_reg;
        }

        if (sym->is_const) {
            fprintf(stderr, _("Error: cannot assign to constant array '%s'\n"), sym->name);
            return target_reg;
        }

        compile_expr(val_node, R1);
        emit(EMIT_PUSH(R1));

        emit_array_address(var_node, sym, R2, R13);

        emit(EMIT_POP(R1));
        emit_array_store(sym, R1, R2);

        if (target_reg != R1) {
            emit(EMIT_MOV(target_reg, R1));
        }

        return target_reg;
    }

    if (var_node->type != AST_IDENT || !var_node->val.str) {
        return target_reg;
    }

    const char *var_name = var_node->val.str;
    symb *sym = symtab_lookup(SymTable, var_name);

    if (!sym) {
        fprintf(
            stderr,
            _("Error: Undefined variable '%s'\n"),
            var_name
        );
        return target_reg;
    }

    if (sym->is_array) {
        fprintf(
            stderr,
            _("Error: array '%s' requires an index\n"),
            var_name
        );
        return target_reg;
    }

    if (sym->is_const) {
        fprintf(stderr, _("Error: cannot assign to constant '%s'\n"), var_name);
        return target_reg;
    }

    compile_expr(val_node, target_reg);

    if (sym->scope == SCOPE_GLOBAL) {
        if (!sym->storage_allocated) {
            sym->location = alloc_global_storage(4);

            if (sym->location < 0) {
                return target_reg;
            }

            sym->storage_allocated = 1;
        }

        emit(
            EMIT_STORE_MEM(
                target_reg,
                FP,
                global_fp_offset(sym->location)
            )
        );
    } else {
        emit(EMIT_STORE_MEM(target_reg, FP, sym->location));
    }

    return target_reg;
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
                fprintf(
                    stderr,
                    _("Error: Undefined variable '%s'\n"),
                    node->val.str ? node->val.str : "?"
                );

                emit(EMIT_LOAD(target_reg, 0));
                return target_reg;
            }

            if (sym->is_array) {
                fprintf(
                    stderr,
                    _("Error: array '%s' requires an index\n"),
                    node->val.str
                );

                emit(EMIT_LOAD(target_reg, 0));
                return target_reg;
            }

            if (sym->scope == SCOPE_GLOBAL) {
                if (!sym->storage_allocated) {
                    sym->location = alloc_global_storage(4);

                    if (sym->location < 0) {
                        emit(EMIT_LOAD(target_reg, 0));
                        return target_reg;
                    }

                    sym->storage_allocated = 1;
                }

                emit(
                    EMIT_LOAD_MEM(
                        target_reg,
                        FP,
                        global_fp_offset(sym->location)
                    )
                );
            } else {
                emit(EMIT_LOAD_MEM(target_reg, FP, sym->location));
            }

            return target_reg;
        }

        case AST_ARRAY_INDEX: {
            symb *sym = NULL;

            if (find_array_symbol(node, &sym) != 0) {
                emit(EMIT_LOAD(target_reg, 0));
                return target_reg;
            }

            int index_reg = (target_reg == R1) ? R2 : R1;

            emit_array_address(node, sym, target_reg, index_reg);

            if (sym->elem_size == 1) {
                emit(EMIT_LOADB_MEM(target_reg, target_reg, 0));
            } else {
                emit(EMIT_LOAD_MEM(target_reg, target_reg, 0));
            }

            return target_reg;
        }

        case AST_ASSIGN_STMT:
            return compile_assignment_node(node, target_reg);

        case AST_NEGATIVE: {
            nu_ast_node_t *operand = node->first_child;

            if (operand) {
                int scratch = (target_reg == R1) ? R2 : R1;

                compile_expr(operand, scratch);
                emit(EMIT_LOAD(target_reg, 0));
                emit(EMIT_SUB(target_reg, target_reg, scratch));
            }

            return target_reg;
        }

        case AST_BNOT: {
            nu_ast_node_t *operand = node->first_child;

            if (operand) {
                compile_expr(operand, target_reg);
                emit(EMIT_BNOT(target_reg, target_reg));
            }

            return target_reg;
        }

        case AST_LNOT: {
            nu_ast_node_t *operand = node->first_child;

            if (operand) {
                int scratch = (target_reg == R1) ? R2 : R1;

                compile_expr(operand, target_reg);

                emit(EMIT_LOAD(scratch, 0));
                emit(EMIT_SUB(scratch, scratch, target_reg));
                emit(EMIT_BOR(target_reg, target_reg, scratch));
                emit(EMIT_SHR(target_reg, 0, 31));

                emit(EMIT_LOAD(scratch, 1));
                emit(EMIT_SUB(scratch, scratch, target_reg));
                emit(EMIT_LOAD(target_reg, 0));
                emit(EMIT_ADD(target_reg, target_reg, scratch));
            }

            return target_reg;
        }

        case AST_CAST: {
            nu_ast_node_t *operand = node->first_child;

            if (operand) {
                compile_expr(operand, target_reg);

                if (node->val.str &&
                    strcmp(node->val.str, "char") == 0) {
                    emit(EMIT_BAND(target_reg, target_reg, R14));
                }
            }

            return target_reg;
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
            nu_ast_node_t *right = left
                ? left->next_sibling
                : NULL;

            if (!left || !right) {
                return target_reg;
            }

            compile_expr(left, target_reg);

            int right_reg = (target_reg == R1) ? R2 : R1;

            emit(EMIT_PUSH(target_reg));
            compile_expr(right, right_reg);
            emit(EMIT_POP(target_reg));

            switch (node->type) {
                case AST_ADD:
                    emit(EMIT_ADD(target_reg, target_reg, right_reg));
                    break;

                case AST_SUB:
                    emit(EMIT_SUB(target_reg, target_reg, right_reg));
                    break;

                case AST_MUL:
                    emit(EMIT_MUL(target_reg, target_reg, right_reg));
                    break;

                case AST_DIV:
                    emit(EMIT_DIV(target_reg, target_reg, right_reg));
                    break;

                case AST_MOD:
                    emit(EMIT_MOD(target_reg, target_reg, right_reg));
                    break;

                case AST_BAND:
                    emit(EMIT_BAND(target_reg, target_reg, right_reg));
                    break;

                case AST_BOR:
                    emit(EMIT_BOR(target_reg, target_reg, right_reg));
                    break;

                case AST_BXOR:
                    emit(EMIT_BXOR(target_reg, target_reg, right_reg));
                    break;

                case AST_SHL:
                    emit(EMIT_SHLR(target_reg, right_reg));
                    break;

                case AST_SHR:
                    emit(EMIT_SHRR(target_reg, right_reg));
                    break;

                default:
                    break;
            }

            return target_reg;
        }

        case AST_FUNC_CALL: {
            nu_ast_node_t *target_fn = NULL;

            for (nu_ast_node_t *child = g_root_node->first_child;
                 child != NULL;
                 child = child->next_sibling) {
                if (child->type == AST_FUNC_DECL &&
                    child->val.str &&
                    node->val.str &&
                    strcmp(child->val.str, node->val.str) == 0) {
                    target_fn = child;
                    break;
                }
            }

            if (!target_fn) {
                fprintf(
                    stderr,
                    _("Runtime Error: Undefined function '%s'\n"),
                    node->val.str ? node->val.str : "?"
                );

                emit(EMIT_LOAD(target_reg, 0));
                return target_reg;
            }

            nu_ast_node_t *param_list = NULL;
            nu_ast_node_t *block = NULL;

            for (nu_ast_node_t *child = target_fn->first_child;
                 child != NULL;
                 child = child->next_sibling) {
                if (child->type == AST_PARAM_LIST) {
                    param_list = child;
                }

                if (child->type == AST_BLOCK) {
                    block = child;
                }
            }

            nu_ast_node_t *param = param_list
                ? param_list->first_child
                : NULL;

            nu_ast_node_t *arg = node->first_child;

            while (param && arg) {
                const char *param_name = param->val.str;

                if (!param_name) {
                    for (nu_ast_node_t *pchild = param->first_child;
                         pchild != NULL;
                         pchild = pchild->next_sibling) {
                        if (pchild->type == AST_PARAM_NAME) {
                            param_name = pchild->val.str;
                            break;
                        }
                    }
                }

                if (param_name) {
                    symb *sym = symtab_lookup(SymTable, param_name);

                    if (!sym) {
                        sym = symtab_add(SymTable, param_name, VAR_INT);
                    }

                    if (sym) {
                        sym->scope = SCOPE_LOCAL;

                        if (!sym->storage_allocated) {
                            sym->location = alloc_stack_offset(4);
                            sym->storage_allocated = 1;
                        }

                        compile_expr(arg, R1);
                        emit(EMIT_STORE_MEM(R1, FP, sym->location));
                    }
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

        case AST_FFI_CALL: {
            nu_ast_node_t *alias_node = node->first_child;

            if (!alias_node ||
                alias_node->type != AST_IDENT ||
                !alias_node->val.str) {
                fprintf(stderr, _("FFI error: malformed module call\n"));
                emit(EMIT_LOAD(target_reg, 0));
                return target_reg;
            }

            const char *alias = alias_node->val.str;
            const char *library = ffi_library_for_alias(alias);

            if (!library || !node->val.str) {
                fprintf(
                    stderr,
                    _("FFI error: invalid library or symbol for '%s'\n"),
                    alias
                );

                emit(EMIT_LOAD(target_reg, 0));
                return target_reg;
            }

            int library_id = register_runtime_string(library);
            int symbol_id = register_runtime_string(node->val.str);

            if (library_id < 0 || symbol_id < 0) {
                emit(EMIT_LOAD(target_reg, 0));
                return target_reg;
            }

            emit(EMIT_LOAD(R0, library_id));
            emit(EMIT_LOAD(R1, symbol_id));
            emit(EMIT_SYS(PAW_SYS_FFI_LOOKUP));
            emit(EMIT_PUSH(R0));

            nu_ast_node_t *arg = alias_node->next_sibling;
            uint32_t argc = 0;
            uint64_t packed_types = ffi_pack_types(arg, &argc);

            emit(EMIT_PUSHI(argc));
            emit(EMIT_PUSHI((uint32_t)packed_types));
            emit(EMIT_PUSHI((uint32_t)(packed_types >> 32)));
            emit(EMIT_SYS(PAW_SYS_FFI_CHECK));

            arg = alias_node->next_sibling;

            while (arg) {
                compile_ffi_argument(arg, R1);
                emit(EMIT_PUSH(R1));
                arg = arg->next_sibling;
            }

            for (int i = (int)argc; i >= 1; --i) {
                emit(EMIT_POP(R0 + i));
            }

            emit(EMIT_POP(R0));
            emit(EMIT_SYS(PAW_SYS_FFI_CALL));

            if (target_reg != R0) {
                emit(EMIT_MOV(target_reg, R0));
            }

            return target_reg;
        }

        default:
            return target_reg;
    }
}

static void removequotes(const char *in, char *out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (!in) {
        out[0] = '\0';
        return;
    }

    size_t len = strlen(in);

    if (len >= 2) {
        char first = in[0];
        char last = in[len - 1];

        if ((first == '\'' && last == '\'') ||
            (first == '"' && last == '"')) {
            size_t inner = len - 2;

            if (inner >= out_size) {
                inner = out_size - 1;
            }

            memcpy(out, in + 1, inner);
            out[inner] = '\0';
            return;
        }
    }

    strncpy(out, in, out_size - 1);
    out[out_size - 1] = '\0';
}

static void unescape(const char *in, char *out, size_t out_sz) {
    size_t j = 0;

    for (size_t i = 0;
         in[i] != '\0' && j + 1 < out_sz;
         i++) {
        if (in[i] == '\\') {
            char n = in[i + 1];

            if (n == '\0') {
                break;
            }

            switch (n) {
                case 'n': out[j++] = '\n'; i++; break;
                case 't': out[j++] = '\t'; i++; break;
                case 'r': out[j++] = '\r'; i++; break;
                case '0': out[j++] = '\0'; i++; break;
                case 'a': out[j++] = '\a'; i++; break;
                case 'b': out[j++] = '\b'; i++; break;
                case 'f': out[j++] = '\f'; i++; break;
                case 'v': out[j++] = '\v'; i++; break;
                case '\\': out[j++] = '\\'; i++; break;
                case '"': out[j++] = '"'; i++; break;
                case '\'': out[j++] = '\''; i++; break;
                default: out[j++] = n; i++; break;
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
        case AST_ROOT:
            for (nu_ast_node_t *child = node->first_child;
                 child != NULL;
                 child = child->next_sibling) {
                compile_node(child);
            }
            break;

        case AST_FUNC_DECL: {
            bool prev_in_func = in_function;
            int prev_frame_bytes = current_frame_bytes;

            in_function = true;
            current_frame_bytes = 0;

            for (nu_ast_node_t *child = node->first_child;
                 child != NULL;
                 child = child->next_sibling) {
                if (child->type == AST_BLOCK) {
                    compile_node(child);
                }
            }

            current_frame_bytes = prev_frame_bytes;
            in_function = prev_in_func;
            break;
        }

        case AST_FUNC_CALL:
        case AST_FFI_CALL:
            compile_expr(node, R0);
            break;

        case AST_EXTERN_DECL:
        case AST_LIB_DECL:
            break;

        case AST_PRINTF_STMT: {
            nu_ast_node_t *fmt_node = node->first_child;

            if (!fmt_node) {
                break;
            }

            size_t in_len = strlen(fmt_node->val.str);
            char *val = nu_alloc(g_mm, in_len + 1);

            if (!val) {
                break;
            }

            removequotes(fmt_node->val.str, val, in_len + 1);

            char *realfmt = nu_alloc(g_mm, in_len + 1);

            if (!realfmt) {
                nu_free(g_mm, val);
                break;
            }

            unescape(val, realfmt, in_len + 1);

            int fmt_id = vm_register_format(realfmt);

            emit(EMIT_LOAD(R0, fmt_id));

            int count = 0;
            int reg_base = 1;

            nu_ast_node_t *arg = fmt_node->next_sibling;

            while (arg && count < MAX_REGS - 2) {
                compile_expr(arg, reg_base + count);
                count++;
                arg = arg->next_sibling;
            }

            emit(EMIT_SYS(PAW_SYS_PRINTF));

            nu_free(g_mm, val);
            nu_free(g_mm, realfmt);
            break;
        }

        case AST_PRINT_STMT: {
            nu_ast_node_t *expr = node->first_child;

            if (!expr) {
                break;
            }

            if (expr->type == AST_FFI_CALL) {
                compile_expr(expr, R0);
                emit(EMIT_SYS(PAW_SYS_FFI_PRINT));
                break;
            }

            if (expr->type == AST_CONST &&
                expr->val.str &&
                expr->val.str[0] == '"') {
                size_t orig_len = strlen(expr->val.str);

                char *val = nu_alloc(g_mm, orig_len + 1);
                char *realfmt = nu_alloc(g_mm, orig_len + 1);

                if (val && realfmt) {
                    removequotes(expr->val.str, val, orig_len + 1);
                    unescape(val, realfmt, orig_len + 1);

                    int str_id = vm_register_string(realfmt);

                    emit(EMIT_LOAD(R0, str_id));
                    emit(EMIT_SYS(PAW_SYS_PRINT_STRING));
                }

                if (val) {
                    nu_free(g_mm, val);
                }

                if (realfmt) {
                    nu_free(g_mm, realfmt);
                }

                break;
            }

            const char *fmt_str = "%d\n";

            if (expr_var_type(expr) == VAR_CHAR) {
                fmt_str = "%c\n";
            }

            int fmt_id = vm_register_format(fmt_str);

            compile_expr(expr, R1);
            emit(EMIT_LOAD(R0, fmt_id));
            emit(EMIT_SYS(PAW_SYS_PRINTF));
            break;
        }

        case AST_CONST_DECL:
        case AST_CHAR_DECL:
        case AST_INT_DECL: {
            nu_ast_node_t *var_node = node->first_child;
            nu_ast_node_t *val_node = var_node
                ? var_node->next_sibling
                : NULL;

            const char *var_name =
                var_node && var_node->val.str
                    ? var_node->val.str
                    : "?";

            symb *sym = symtab_lookup(SymTable, var_name);

            if (!sym) {
                var_type_t type =
                    node->type == AST_CHAR_DECL
                        ? VAR_CHAR
                        : VAR_INT;

                sym = symtab_add(SymTable, var_name, type);

                if (!sym) {
                    break;
                }
            }

            nu_ast_node_t *size_node = array_size_node(node);

            if (size_node || sym->is_array) {
                compile_array_decl(node, sym);
                break;
            }

            if (in_function) {
                sym->scope = SCOPE_LOCAL;

                if (!sym->storage_allocated) {
                    sym->location = alloc_stack_offset(4);

                    if (sym->location == 0) {
                        break;
                    }

                    sym->storage_allocated = 1;
                }

                if (val_node) {
                    compile_expr(val_node, R1);
                    emit(EMIT_STORE_MEM(R1, FP, sym->location));
                } else {
                    emit(EMIT_LOAD(R1, 0));
                    emit(EMIT_STORE_MEM(R1, FP, sym->location));
                }
            } else {
                sym->scope = SCOPE_GLOBAL;

                if (!sym->storage_allocated) {
                    sym->location = alloc_global_storage(4);

                    if (sym->location < 0) {
                        break;
                    }

                    sym->storage_allocated = 1;
                }

                if (val_node) {
                    sym->val = get_node_value(val_node);

                    compile_expr(val_node, R1);

                    emit(
                        EMIT_STORE_MEM(
                            R1,
                            FP,
                            global_fp_offset(sym->location)
                        )
                    );
                } else {
                    sym->val = 0;

                    emit(EMIT_LOAD(R1, 0));

                    emit(
                        EMIT_STORE_MEM(
                            R1,
                            FP,
                            global_fp_offset(sym->location)
                        )
                    );
                }
            }

            break;
        }

        case AST_BLOCK:
            for (nu_ast_node_t *stmt = node->first_child;
                 stmt != NULL;
                 stmt = stmt->next_sibling) {
                compile_node(stmt);
            }
            break;

        case AST_RETURN_STMT: {
            nu_ast_node_t *val_node = node->first_child;

            if (val_node) {
                compile_expr(val_node, R0);
            }

            break;
        }

        case AST_ASSIGN_STMT:
            compile_expr(node, R1);
            break;

        default:
            for (nu_ast_node_t *child = node->first_child;
                 child != NULL;
                 child = child->next_sibling) {
                compile_node(child);
            }
            break;
    }
}

bool write_bytecode_file(const char *filename, const BytecodeBuffer *buf) {
    if (!buf || !filename) {
        return false;
    }

    FILE *f = fopen(filename, "wb");

    if (!f) {
        return false;
    }

    uint32_t str_count = vm_get_string_count();

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

    fwrite(
        buf->instructions,
        sizeof(Instruction),
        buf->count,
        f
    );

    fclose(f);
    return true;
}

void dogma_ast2file(nu_ast_node_t *node, const char *out_filename) {
    if (!node) return;

    g_root_node = node;
    code_buf = NULL;
    in_function = false;
    current_frame_bytes = 0;
    global_data_offset = GLOBAL_DATA_BASE;

    emit(EMIT_LOAD(FP, RAM_SIZE - 4));

    nu_ast_node_t *main_fn = NULL;

    for (nu_ast_node_t *child = node->first_child;
         child != NULL;
         child = child->next_sibling) {
        if (child->type == AST_FUNC_DECL &&
            child->val.str &&
            strcmp(child->val.str, "main") == 0) {
            main_fn = child;
            break;
        }
    }

    if (main_fn) {
        for (nu_ast_node_t *child = node->first_child;
             child != NULL;
             child = child->next_sibling) {
            if (child->type == AST_CONST_DECL ||
                child->type == AST_INT_DECL ||
                child->type == AST_CHAR_DECL) {
                compile_node(child);
            }
        }

        compile_node(main_fn);
    } else {
        for (nu_ast_node_t *child = node->first_child;
             child != NULL;
             child = child->next_sibling) {
            compile_node(child);
        }
    }

    emit(EMIT_HALT(R0));

    if (code_buf && code_buf->count > 0) {
        write_bytecode_file(out_filename, code_buf);

        nu_free(
            g_mm,
            code_buf->instructions
        );

        nu_free(g_mm, code_buf);
        code_buf = NULL;
    }
}
