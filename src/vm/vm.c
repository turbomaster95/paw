#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vm.h>
#include <string.h>
#include <nu.h>

#define NEED_FORMAT
#include <common.h>

static void vm_exec_printf(const int32_t *regs, int reg_base, int arg_count, const char *fmt) {
    if (!fmt) return;

    int arg_idx = 0;
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            putchar(*p++);
            continue;
        }

        p++; // Skip '%'

        if (*p == '%') {
            putchar('%');
            p++;
            continue;
        }

        char spec[32];
        int len = 0;
        spec[len++] = '%';

        while (*p && !strchr("diuoxXcspfeEgGaA", *p) && len < 30) {
            spec[len++] = *p++;
        }

        if (*p) {
            char conversion = *p++;
            spec[len++] = conversion;
            spec[len] = '\0';

            if (arg_idx < arg_count) {
                int32_t raw_val = regs[reg_base + arg_idx++];

                if (conversion == 's') {
                    const char *str = vm_get_string((uint32_t)raw_val);
                    printf(spec, str ? str : "(null)");
                } else if (conversion == 'c') {
                    printf(spec, (char)raw_val);
                } else {
                    printf(spec, raw_val);
                }
            } else {
                fputs(spec, stdout);
            }
        }
    }
}

int32_t run_paw_vm(const Instruction *code) {
    int32_t R[NUM_REGS] = {0};
    size_t ip = 0;

    CallFrame call_stack[MAX_CALL_DEPTH];
    size_t sp = 0;

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
                printf("%s\n", str ? str : "(null)");
                break;
            }
            case OP_PRINTF: {
                int reg = r1;
                int count = r2;
                int fmt_id = r3;

                const char* fmt = vm_get_string(fmt_id);

                vm_exec_printf(R, reg, count, fmt);
                break;
            }
            case OP_CALL: {
                if (sp >= MAX_CALL_DEPTH) {
                    fprintf(stderr, "Stack overflow\n");
                    exit(1);
                }

                call_stack[sp].return_ip = ip;
                call_stack[sp].dest_reg = r1;

                memcpy(call_stack[sp].saved_regs, R, sizeof(int32_t) * 16);
                sp++;

                int32_t args[16];
                for (int i = 0; i < r3; i++) {
                    args[i] = R[r2 + i];
                }
                for (int i = 0; i < r3; i++) {
                    R[i] = args[i];
                }

                ip = imm;
                break;
            }

            case OP_RET: {
                if (sp == 0) {
                    return R[r1];
                }

                int32_t retval = R[r1];
                sp--;

                memcpy(R, call_stack[sp].saved_regs, sizeof(int32_t) * 16);

                R[call_stack[sp].dest_reg] = retval;

                ip = call_stack[sp].return_ip;
                break;
            }
            case OP_HALT:
                return R[r1];
            default:
                exit(1);
        }
    }
}

