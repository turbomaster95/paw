#ifndef PAWV_H
#define PAWV_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NOSRY_GLUE_GLOBAL
#include <vmglue.h>
#include <nosry/vm.h>

#define NUM_REGS MAX_REGS

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

typedef Inst Instruction;

typedef struct {
    Instruction *instructions;
    size_t count;
    size_t capacity;
} BytecodeBuffer;

#define ENCODE_I(op, r1, imm_val)    ((Inst){ .opcode = (u8)(op), .dest = (u8)(r1), .imm = (i32)(imm_val) })
#define ENCODE_R(op, r1, r2, r3_val) ((Inst){ .opcode = (u8)(op), .dest = (u8)(r1), .src = (u8)(r2), .reserved = (u8)(r3_val) })

#define GET_OP(inst)   ((inst).opcode)
#define GET_R1(inst)   ((inst).dest)
#define GET_R2(inst)   ((inst).src)
#define GET_R3(inst)   ((inst).reserved)
#define GET_IMM(inst)  ((inst).imm)

#define EMIT_LOAD(dest_r, imm_val)       INST_MOV(dest_r, imm_val)
#define EMIT_MOV(dest_r, src_r)          INST_MOVR(dest_r, src_r)
#define EMIT_ADD(dest_r, src1_r, src2_r) INST_ADD(dest_r, src2_r)
#define EMIT_SUB(dest_r, src1_r, src2_r) INST_SUB(dest_r, src2_r)
#define EMIT_HALT(src_r)                 ((Inst){ .opcode = OP_HALT })

#ifdef OP_PRINT
  #define EMIT_PRINT(src_r)               ENCODE_R(OP_PRINT, src_r, 0, 0)
#else
  #define EMIT_PRINT(src_r)               ENCODE_R(OP_SYS, src_r, 0, 0)
#endif

#ifdef OP_PRINTF
  #define EMIT_PRINTF(base_r, count, fmt) ENCODE_R(OP_PRINTF, base_r, count, fmt)
#else
  #define EMIT_PRINTF(base_r, count, fmt) ENCODE_R(OP_SYS, base_r, count, fmt)
#endif

int32_t run_paw_vm(VM *vm, Memory *mem, size_t prog_len);

#endif // PAWV_H

