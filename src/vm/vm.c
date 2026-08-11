#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vm.h>
#include <string.h>
#include <nu.h>

#define NEED_FORMAT
#include <common.h>

int32_t run_paw_vm(const Instruction *code) {
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
            case OP_PRINT: {
                int str_id = R[r1];
                const char* str = vm_get_string(str_id);
                printf("%s\n", str);
                break;
            }
            case OP_PRINTF: {
                int reg = r1;
                int count = r2;
                int fmt_id = r3;

                const char* fmt = vm_get_string(fmt_id);
                char buf[4096];

                switch (count) {
                    case 0: snprintf(buf, sizeof(buf), "%s", fmt); break;
                    case 1: snprintf(buf, sizeof(buf), fmt, R[reg+0]); break;
                    case 2: snprintf(buf, sizeof(buf), fmt, R[reg+0], R[reg+1]); break;
                    case 3: snprintf(buf, sizeof(buf), fmt, R[reg+0], R[reg+1], R[reg+2]); break;
                    default: snprintf(buf, sizeof(buf), fmt, R[reg+0], R[reg+1], R[reg+2], R[reg+3]); break;
                }
                printf("%s", buf);
                break;
            }
            case OP_HALT:
                return R[r1]; // Return the value given through r1
            default:
                exit(1);
        }
    }
}

