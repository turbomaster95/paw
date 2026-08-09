#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vm.h>

void run_paw_vm(const Instruction *code) {
    int32_t R[NUM_REGS] = {0};
    size_t ip = 0;

    while (1) {
        Instruction inst = code[ip++];

        uint8_t op  = GET_OP(inst);
        uint8_t r1  = GET_R1(inst);
        uint8_t r2  = GET_R2(inst);
        uint8_t r3  = GET_R3(inst);
        int32_t imm = GET_IMM(inst);

        switch (op) {
            case OP_LOAD:
                R[r1] = imm;
                break;
            case OP_MOV:
                R[r1] = R[r2];
                break;
            case OP_ADD:
                R[r1] = R[r2] + R[r3];
                break;
            case OP_SUB:
                R[r1] = R[r2] - R[r3];
                break;
            case OP_MUL:
                R[r1] = R[r2] * R[r3];
                break;
            case OP_DIV:
                if (R[r3] == 0) exit(1);
                R[r1] = R[r2] / R[r3];
                break;
            case OP_JMP:
                ip = imm;
                break;
            case OP_JMPEQ:
                if (R[r1] == R[r2]) ip = imm;
                break;
            case OP_JMPLT:
                if (R[r1] < R[r2]) ip = imm;
                break;
            case OP_PRINT:
                printf("%d\n", R[r1]);
                break;
            case OP_HALT:
                return;
            default:
                exit(1);
        }
    }
}
