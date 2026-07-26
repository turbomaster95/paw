#include <stdio.h>
#include <stdlib.h>
#include <vm.h>

void chunk_init(Chunk *chunk) {
    chunk->code = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->constants = NULL;
    chunk->const_count = 0;
    chunk->const_capacity = 0;
}

void chunk_free(Chunk *chunk) {
    free(chunk->code);
    free(chunk->constants);
    chunk_init(chunk);
}

void chunk_write(Chunk *chunk, uint8_t byte) {
    if (chunk->capacity < chunk->count + 1) {
        size_t old_cap = chunk->capacity;
        chunk->capacity = old_cap < 8 ? 8 : old_cap * 2;
        chunk->code = realloc(chunk->code, sizeof(uint8_t) * chunk->capacity);
    }
    chunk->code[chunk->count++] = byte;
}

int chunk_add_constant(Chunk *chunk, Value value) {
    if (chunk->const_capacity < chunk->const_count + 1) {
        size_t old_cap = chunk->const_capacity;
        chunk->const_capacity = old_cap < 8 ? 8 : old_cap * 2;
        chunk->constants = realloc(chunk->constants, sizeof(Value) * chunk->const_capacity);
    }
    chunk->constants[chunk->const_count] = value;
    return (int)chunk->const_count++;
}
