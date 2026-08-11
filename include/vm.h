#ifndef PAWV_H
#define PAWV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NUM_REGS 16

#define R0  0
#define R1  1
#define R2  2
#define R3  3
#define R4  4
#define R5  5
#define R6  6
#define R7  7
#define R8  8
#define R9  9
#define R10 10
#define R11 11
#define R12 12
#define R13 13
#define R14 14
#define R15 15

typedef struct {
  char magic[4];
  uint16_t version;
  uint16_t reserved; // MUST be 0x0000 always
  uint64_t inst_count;
} PawHdr;

typedef enum {
    OP_LOAD,
    OP_MOV,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_JMP,
    OP_JMPEQ,
    OP_JMPLT,
    OP_PRINT,
    OP_PRINTF,
    OP_HALT
} Opcode;

typedef uint64_t Instruction;

typedef struct {
    Instruction *instructions;
    size_t count;
    size_t capacity;
} BytecodeBuffer;

#define ENCODE_I(op, r1, imm)        (((uint64_t)(op) << 56) | ((uint64_t)(r1) << 48) | ((uint64_t)(uint32_t)(imm)))
#define ENCODE_R(op, r1, r2, r3)     (((uint64_t)(op) << 56) | ((uint64_t)(r1) << 48) | ((uint64_t)(r2) << 40) | ((uint64_t)(r3) << 32))
#define ENCODE_J(op, r1, r2, imm)    (((uint64_t)(op) << 56) | ((uint64_t)(r1) << 48) | ((uint64_t)(r2) << 40) | ((uint64_t)(uint32_t)(imm)))

#define LOAD(reg, val)       ENCODE_I(OP_LOAD, reg, val)
#define MOV(dst, src)        ENCODE_R(OP_MOV, dst, src, 0)
#define ADD(dst, r1, r2)     ENCODE_R(OP_ADD, dst, r1, r2)
#define SUB(dst, r1, r2)     ENCODE_R(OP_SUB, dst, r1, r2)
#define MUL(dst, r1, r2)     ENCODE_R(OP_MUL, dst, r1, r2)
#define DIV(dst, r1, r2)     ENCODE_R(OP_DIV, dst, r1, r2)
#define JMP(addr)            ENCODE_I(OP_JMP, 0, addr)
#define JMPEQ(r1, r2, addr)  ENCODE_J(OP_JMPEQ, r1, r2, addr)
#define JMPLT(r1, r2, addr)  ENCODE_J(OP_JMPLT, r1, r2, addr)
#define PRINT(reg)           ENCODE_I(OP_PRINT, reg, 0)
#define PRINTF(reg, count, fmt_id) ENCODE_R(OP_PRINTF, reg, count, fmt_id)
#define HALT(reg)            ENCODE_I(OP_HALT, reg, 0)

#define GET_OP(inst)   ((uint8_t)(((inst) >> 56) & 0xFF))
#define GET_R1(inst)   ((uint8_t)(((inst) >> 48) & 0xFF))
#define GET_R2(inst)   ((uint8_t)(((inst) >> 40) & 0xFF))
#define GET_R3(inst)   ((uint8_t)(((inst) >> 32) & 0xFF))
#define GET_IMM(inst)  ((int32_t)((inst) & 0xFFFFFFFF))

int32_t run_paw_vm(const Instruction *code);
int vm_register_format(const char* s);
int vm_register_string(const char* s);
uint32_t vm_get_string_count(void);
const char* vm_get_string(uint32_t id);

#endif

