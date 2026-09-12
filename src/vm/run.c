#define GLOG_IMPL
#include <glog.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <nu.h>
#include <vm.h>
#include <dyncall.h>
#include <dynload.h>

#define NEED_BASENAME
#include <common.h>

nu_mm_t *g_mm = NULL;
char backing[1024 * 1024 * 8]; // 8 MB arena
char *current_filename = NULL;
static DLLib* g_libc_lib = NULL;
DCCallVM* g_vm_ffi = NULL;

static void custom_syscalls(VM *vm, Memory *mem, u32 sys_code) {
    extern DCCallVM* g_vm_ffi;
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

	case 3: { // DLSYM: Lookup C function pointer, R0 = stridx of function name (e.g "abs"), Stores resolved void* pointer into R0
            if (!g_libc_lib) {
                g_libc_lib = dlLoadLibrary(NULL);
            }
            const char *sym_name = VM_get_string(vm, vm->regs[0]);
            void* symbol = dlFindSymbol(g_libc_lib, sym_name);
            vm->regs[0] = (uintptr_t)symbol;
            break;
        }

        case 4: { // CALL_NATIVE: Execute dynamic function via dyncall, R0 = Target func ptr
     	    // R1 = Return type (0 = int, 1 = double, 2 = pointer)
            // R2 = Argument count
            // R3..R(3+N) = Arguments to pass
            void* fn_ptr = (void*)(uintptr_t)vm->regs[0];
            uint32_t ret_type = vm->regs[1];
            uint32_t arg_count = vm->regs[2];

            if (!fn_ptr) {
                printf("Fault: NULL function pointer in CALL_NATIVE\n");
                vm->is_running = 0;
                break;
            }

            dcReset(g_vm_ffi);

            for (uint32_t i = 0; i < arg_count; i++) {
                dcArgInt(g_vm_ffi, (DCint)vm->regs[3 + i]);
            }

            switch (ret_type) {
                case 0: // int / uint32_t
                    vm->regs[0] = (uint32_t)dcCallInt(g_vm_ffi, fn_ptr);
                    break;
                case 1: // double / float
                    vm->regs[0] = (uint32_t)dcCallInt(g_vm_ffi, fn_ptr);
                    break;
                case 2: // pointer return (e.g., char*)
                    vm->regs[0] = (uintptr_t)dcCallPointer(g_vm_ffi, fn_ptr);
                    break;
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

    g_vm_ffi = dcNewCallVM(4096);
    dcMode(g_vm_ffi, DC_CALL_C_DEFAULT);

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
    if (g_libc_lib) dlFreeLibrary(g_libc_lib);
    if (g_vm_ffi) dcFree(g_vm_ffi);
    nu_mm_destroy(g_mm);
    return ret;
}

