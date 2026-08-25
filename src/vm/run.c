#define GLOG_IMPL
#include <glog.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <nu.h>
#include <vm.h>

#define NEED_BASENAME
#include <common.h>

nu_mm_t *g_mm = NULL;
char backing[1024 * 1024 * 8]; // 8 MB arena
char *current_filename = NULL;

static void custom_syscalls(VM *vm, Memory *mem, u32 sys_code) {
    switch (sys_code) {
        case 0: // PUTCHAR: Char in R0
            putchar((char)vm->regs[0]);
            break;

        case 1: { // PUTS: String index in R0
            const char *str = VM_get_string(vm, vm->regs[0]);
            if (str) {
                fputs(str, stdout);
            }
            break;
        }

        case 2: { // PRINTF: Format string index in R0, variadic args in R1, R2...
            const char *fmt = VM_get_string(vm, vm->regs[0]);
            if (fmt) {
                VM_printf(vm, mem, 1, 15, fmt);
            }
            break;
        }

        default:
            printf("Fault: Unhandled System Call %u\n", sys_code);
            vm->is_running = 0;
            break;
    }
}

int main(int argc, char **argv) {
    glog_init();
    glog_config.use_color = 1;

    if (argc < 2) {
        glog_log(NULL, 0, 0, GLOG_INFO, "Usage: %s <bytecode.pawv>\n", get_basename(argv[0]));
        return EXIT_FAILURE;
    }

    g_mm = nu_mm_create(NU_MM_ARENA, backing, sizeof(backing));
    if (!g_mm) {
        glog_log(NULL, 0, 0, GLOG_FATAL, "Fatal: Failed to allocate memory arena!");
        return EXIT_FAILURE;
    }

    Memory *mem = nu_alloc(g_mm, sizeof(Memory));
    VM *vm = nu_alloc(g_mm, sizeof(VM));

    if (!mem || !vm) {
        glog_log(NULL, 0, 0, GLOG_FATAL, "Fatal: Out of memory!");
        nu_mm_destroy(g_mm);
        return EXIT_FAILURE;
    }

    VM_reset(vm, mem);

    current_filename = argv[1];
    u32 load_vaddr = 0x00;
    vm->syscall_handler = custom_syscalls;

    int ret = VM_run_file(current_filename, mem, vm, load_vaddr);
    if (ret != 0) {
        glog_log(NULL, 0, 0, GLOG_FATAL, "Couldn't run bytecode file: %s , errcode: %d", current_filename, ret);
        nu_mm_destroy(g_mm);
        return EXIT_FAILURE;
    }

    VM_clear_strings(vm);
    nu_mm_destroy(g_mm);
    return ret;
}

