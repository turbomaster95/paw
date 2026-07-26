#include <stdio.h>
#include "vm.h"

static int simple_instruction(const char *name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int byte_instruction(const char *name, Chunk *chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

static int constant_instruction(const char *name, Chunk *chunk, int offset) {
    uint8_t constant_idx = chunk->code[offset + 1];
    printf("%-16s %4d '", name, constant_idx);
    printf("%d", chunk->constants[constant_idx]);
    printf("'\n");
    return offset + 2;
}

static int jump_instruction(const char *name, int sign, Chunk *chunk, int offset) {
    uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
    jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

void disassemble_chunk(Chunk *chunk, const char *name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < (int)chunk->count;) {
        offset = disassemble_instruction(chunk, offset);
    }
}

int disassemble_instruction(Chunk *chunk, int offset) {
    printf("%04d ", offset);

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_CONSTANT:      return constant_instruction("OP_CONSTANT", chunk, offset);
        case OP_ADD:           return simple_instruction("OP_ADD", offset);
        case OP_SUB:           return simple_instruction("OP_SUB", offset);
        case OP_MUL:           return simple_instruction("OP_MUL", offset);
        case OP_DIV:           return simple_instruction("OP_DIV", offset);
        case OP_NEGATE:        return simple_instruction("OP_NEGATE", offset);
        case OP_EQUAL:         return simple_instruction("OP_EQUAL", offset);
        case OP_GREATER:       return simple_instruction("OP_GREATER", offset);
        case OP_LESS:          return simple_instruction("OP_LESS", offset);
        case OP_POP:           return simple_instruction("OP_POP", offset);
        case OP_GET_LOCAL:     return byte_instruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_LOCAL:     return byte_instruction("OP_SET_LOCAL", chunk, offset);
        case OP_JUMP:          return jump_instruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE: return jump_instruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:          return jump_instruction("OP_LOOP", -1, chunk, offset);
        case OP_CALL:          return byte_instruction("OP_CALL", chunk, offset);
        case OP_RETURN:        return simple_instruction("OP_RETURN", offset);
        case OP_HALT:          return simple_instruction("OP_HALT", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}
