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

#define EMIT_NOP()                         INST_NOP()
#define EMIT_HALT(src_r)                   INST_HALT()
#define EMIT_SYS(sys_code)                 INST_SYS(sys_code)

#define EMIT_LOAD(dest_r, imm_val)         INST_MOV(dest_r, imm_val)
#define EMIT_MOV(dest_r, src_r)            INST_MOVR(dest_r, src_r)

#define EMIT_ADD(dest_r, src1_r, src2_r)   INST_ADD(dest_r, src2_r)
#define EMIT_ADDI(dest_r, src_r, imm_val)  INST_ADDI(dest_r, imm_val)
#define EMIT_SUB(dest_r, src1_r, src2_r)   INST_SUB(dest_r, src2_r)
#define EMIT_SUBI(dest_r, src_r, imm_val)  INST_SUBI(dest_r, imm_val)
#define EMIT_MUL(dest_r, src1_r, src2_r)   INST_MUL(dest_r, src2_r)
#define EMIT_MULI(dest_r, src_r, imm_val)  INST_MULI(dest_r, imm_val)
#define EMIT_DIV(dest_r, src1_r, src2_r)   INST_DIV(dest_r, src2_r)
#define EMIT_DIVI(dest_r, src_r, imm_val)  INST_DIVI(dest_r, imm_val)
#define EMIT_MOD(dest_r, src1_r, src2_r)   INST_MOD(dest_r, src2_r)

#define EMIT_BAND(dest_r, src1_r, src2_r)  INST_AND(dest_r, src2_r)
#define EMIT_BOR(dest_r, src1_r, src2_r)   INST_OR(dest_r, src2_r)
#define EMIT_BXOR(dest_r, src1_r, src2_r)  INST_XOR(dest_r, src2_r)
#define EMIT_BNOT(dest_r, src_r)           INST_NOT(dest_r)
#define EMIT_SHR(dest_r, src_r, off)       INST_SHR(dest_r, off)
#define EMIT_SHL(dest_r, src_r, off)       INST_SHL(dest_r, off)

#define EMIT_CMP(r1, r2)                   INST_CMP(r1, r2)
#define EMIT_CMPI(r1, imm_val)             INST_CMPI(r1, imm_val)

#define EMIT_JMP(addr)                     INST_JMP(addr)
#define EMIT_JMPO(off_addr)                INST_JMPO(off_addr)
#define EMIT_JZ(addr)                      INST_JZ(addr)
#define EMIT_JNZ(addr)                     INST_JNZ(addr)
#define EMIT_JLT(addr)                     INST_JLT(addr)
#define EMIT_JGT(addr)                     INST_JGT(addr)

#define EMIT_PUSH(src_r)                   INST_PUSH(src_r)
#define EMIT_PUSHI(imm_val)                INST_PUSHI(imm_val)
#define EMIT_POP(dest_r)                   INST_POP(dest_r)

#define EMIT_CALL(addr)                    INST_CALL(addr)
#define EMIT_CALLR(off_addr)               INST_CALLR(off_addr)
#define EMIT_RET()                         INST_RET()

#define EMIT_LOAD_MEM(dest_r, base_r, off) INST_LOAD(dest_r, base_r, off)
#define EMIT_LOAD_PC(dest_r, off)          INST_LOAD_PC(dest_r, off)
#define EMIT_STORE_MEM(src_r, base_r, off) INST_STORE(src_r, base_r, off)
#define EMIT_LOADB_MEM(dest_r, base_r, off) INST_LOADB(dest_r, base_r, off)
#define EMIT_LOADB_PC(dest_r, off)         INST_LOADB_PC(dest_r, off)
#define EMIT_STOREB_MEM(src_r, base_r, off) INST_STOREB(src_r, base_r, off)

int32_t run_paw_vm(VM *vm, Memory *mem, size_t prog_len);

#endif // PAWV_H

