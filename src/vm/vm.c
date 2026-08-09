#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vm.h>
#include <string.h>
#include <nu.h>

#define MAX_STRINGS 1024

static const char* g_fmt_table[MAX_STRINGS];
static int g_fmt_count = 0;

static const char* g_str_table[MAX_STRINGS];
static int g_str_count = 0;

int vm_register_format(const char* s) {
    if (g_fmt_count >= MAX_STRINGS) return -1;
    g_fmt_table[g_fmt_count] = s;
    return g_fmt_count++;
}

int vm_register_string(const char* s) {
    if (g_str_count >= MAX_STRINGS) return -1;
    g_str_table[g_str_count] = s;
    return g_str_count++;
}

uint32_t vm_get_string_count(void) {
    return g_str_count;
}

const char* vm_get_string(uint32_t id) {
    if (id >= g_str_count) {
        return "";
    }
    return g_str_table[id];
}

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
            case OP_PRINT: {
		int str_id = R[r1];
		const char* str = g_str_table[str_id];
                printf("%s\n", str);
                break;
	    }
            case OP_PRINTF: {
		int reg = r1;
		int count = r2;
		int fmt_id = r3;

		const char* fmt = g_fmt_table[fmt_id];
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
                return;
            default:
                exit(1);
        }
    }
}
