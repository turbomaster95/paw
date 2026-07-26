#ifndef PAWV_H
#define PAWV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define STACK_MAX 256
#define FRAMES_MAX 64

typedef int Value;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef enum {
    OP_CONSTANT,      /* Pushes constant onto stack */
    OP_ADD,           /* Pops b, pops a, pushes (a + b) */
    OP_SUB,           /* Pops b, pops a, pushes (a - b) */
    OP_MUL,           /* Pops b, pops a, pushes (a * b) */
    OP_DIV,           /* Pops b, pops a, pushes (a / b) */
    OP_NEGATE,        /* Pops a, pushes (-a) */

    OP_EQUAL,         /* Pops b, pops a, pushes (a == b) */
    OP_GREATER,       /* Pops b, pops a, pushes (a > b) */
    OP_LESS,          /* Pops b, pops a, pushes (a < b) */

    OP_POP,           /* Discards top of stack */
    OP_GET_LOCAL,     /* [OP_GET_LOCAL, slot_idx] -> Pushes frame->slots[slot_idx] */
    OP_SET_LOCAL,     /* [OP_SET_LOCAL, slot_idx] -> Assigns top of stack to frame->slots[slot_idx] */

    OP_JUMP,          /* [OP_JUMP, hi, lo] -> Jumps forward */
    OP_JUMP_IF_FALSE, /* [OP_JUMP_IF_FALSE, hi, lo] -> Jumps forward if top is 0/false */
    OP_LOOP,          /* [OP_LOOP, hi, lo] -> Jumps backward */

    OP_CALL,          /* [OP_CALL, arg_count] -> Executes function call frame */
    OP_RETURN,        /* Returns from current call frame */
    OP_HALT           /* Halts virtual machine */
} OpCode;

typedef struct {
    uint8_t *code;
    size_t count;
    size_t capacity;

    Value *constants;
    size_t const_count;
    size_t const_capacity;
} Chunk;

typedef struct {
    Chunk *chunk;
    uint8_t *ip;
    Value *slots;
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frame_count;

    Value stack[STACK_MAX];
    Value *stack_top;
} VM;

void chunk_init(Chunk *chunk);
void chunk_free(Chunk *chunk);
void chunk_write(Chunk *chunk, uint8_t byte);
int chunk_add_constant(Chunk *chunk, Value value);

void vm_init(VM *vm);
void vm_free(VM *vm);
void vm_push(VM *vm, Value value);
Value vm_pop(VM *vm);
InterpretResult vm_run(VM *vm, Chunk *main_chunk);

void disassemble_chunk(Chunk *chunk, const char *name);
int disassemble_instruction(Chunk *chunk, int offset);

#endif

