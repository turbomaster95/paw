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
                printf("%s\n", str);
                break;
            }
            case OP_PRINTF: {
                int reg = r1;
                int count = r2;
                int fmt_id = r3;

                const char* fmt = vm_get_string(fmt_id);
                char buf[4096];
                char *out = buf;
                size_t remaining = sizeof(buf);
                int arg_idx = 0;

                for (const char *p = fmt; *p && remaining > 1; p++) {
                    if (*p == '%' && *(p + 1) != '\0') {
                        p++;
                        if (*p == '%') {
                            *out++ = '%';
                            remaining--;
                        } else if (*p == 's') {
                            int str_id = (arg_idx < count) ? R[reg + arg_idx++] : -1;
                            const char *s = vm_get_string(str_id);
                            int written = snprintf(out, remaining, "%s", s);
                            if (written > 0) {
                                out += written;
                                remaining -= written;
                            }
                        } else if (*p == 'd' || *p == 'i') {
                            int val = (arg_idx < count) ? R[reg + arg_idx++] : 0;
                            int written = snprintf(out, remaining, "%d", val);
                            if (written > 0) {
                                out += written;
                                remaining -= written;
                            }
                        } else {
                            // fallback
                            *out++ = '%';
                            *out++ = *p;
                            remaining -= 2;
                        }
                    } else {
                        *out++ = *p;
                        remaining--;
                    }
                }
                *out = '\0';
                printf("%s", buf);
                break;
            }
            case OP_CALL: {
                if (sp >= MAX_CALL_DEPTH) {
                    fprintf(stderr, "Stack overflow\n");
                    exit(1);
                }

                // r1 = dest_reg in caller
                // r2 = base reg of args in caller
                // r3 = args count
                // imm = target function's ip

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
                return R[r1]; // Return the value given through r1
            default:
                exit(1);
        }
    }
}

