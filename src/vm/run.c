#define GLOG_IMPL
#include <glog.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <nu.h>
#include <vm.h>
#include <dyncall.h>
#include <dynload.h>
#include <pawffi.h>

#define NEED_BASENAME
#define NEED_FORMAT
#include <common.h>

nu_mm_t *g_mm = NULL;
char backing[1024 * 1024 * 8];
char *current_filename = NULL;
DCCallVM *g_vm_ffi = NULL;

#define MAX_PAW_LIBRARIES 64
#define MAX_PAW_FUNCTIONS 512
#define PAW_FFI_UNKNOWN 7

typedef struct {
    char *requested_name;
    char *resolved_path;
    DLLib *handle;
    const paw_library_t *info;
} paw_loaded_library_t;

typedef struct {
    paw_loaded_library_t *library;
    const paw_ffi_function_t *function;
} paw_runtime_function_t;

static paw_loaded_library_t g_libraries[MAX_PAW_LIBRARIES];
static size_t g_library_count = 0;
static paw_runtime_function_t g_functions[MAX_PAW_FUNCTIONS];
static size_t g_function_count = 0;

static paw_ffi_type_t g_last_ffi_type = PAW_FFI_VOID;
static uintptr_t g_last_ffi_value = 0;
static int g_last_ffi_valid = 0;

static const char *ffi_type_name(paw_ffi_type_t type) {
    switch (type) {
        case PAW_FFI_VOID: return "void";
        case PAW_FFI_INT: return "int";
        case PAW_FFI_CHAR: return "char";
        case PAW_FFI_CSTRING: return "cstring";
        case PAW_FFI_POINTER: return "pointer";
        default: return "unknown";
    }
}

static int ffi_type_valid(paw_ffi_type_t type) {
    return type >= PAW_FFI_VOID && type <= PAW_FFI_POINTER;
}

static int is_paw_library_name(const char *path) {
    if (!path || path[0] != '#') return 0;
    if (path[1] == '\0') return 0;

    for (const char *p = path + 1; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return 0;
    }

    return 1;
}

static char *resolve_library_path(const char *requested) {
    if (!requested) return NULL;

    if (!is_paw_library_name(requested)) return strdup(requested);

    const char *name = requested + 1;
    const char *home = getenv("HOME");

    if (home && *home) {
        size_t needed = strlen(home) + strlen("/.local/share/paw/lib/libpaw_") + strlen(name) + strlen(".so") + 1;
        char *path = malloc(needed);

        if (!path) return NULL;

        snprintf(path, needed, "%s/.local/share/paw/lib/libpaw_%s.so", home, name);

        if (access(path, R_OK) == 0) return path;

        free(path);
    }

    {
        size_t needed = strlen("/usr/share/paw/lib/libpaw_") + strlen(name) + strlen(".so") + 1;
        char *path = malloc(needed);

        if (!path) return NULL;

        snprintf(path, needed, "/usr/share/paw/lib/libpaw_%s.so", name);

        if (access(path, R_OK) == 0) return path;

        free(path);
    }

    return NULL;
}

static paw_loaded_library_t *load_paw_library(const char *requested) {
    if (!requested) return NULL;

    for (size_t i = 0; i < g_library_count; ++i) {
        if (strcmp(g_libraries[i].requested_name, requested) == 0) return &g_libraries[i];
    }

    if (g_library_count >= MAX_PAW_LIBRARIES) {
        fprintf(stderr, "FFI error: maximum number of loaded modules (%d) reached\n", MAX_PAW_LIBRARIES);
        return NULL;
    }

    char *resolved = resolve_library_path(requested);

    if (!resolved) {
        if (is_paw_library_name(requested)) {
            fprintf(stderr, "FFI error: module '%s' was not found in the Paw module search paths\n", requested);
        } else {
            fprintf(stderr, "FFI error: module '%s' was not found\n", requested);
        }

        return NULL;
    }

    DLLib *handle = dlLoadLibrary(resolved);

    if (!handle) {
        fprintf(stderr, "FFI error: failed to load module '%s'\n", resolved);
        free(resolved);
        return NULL;
    }

    void *info_symbol = dlFindSymbol(handle, "paw_library_info");

    if (!info_symbol) {
        fprintf(stderr, "FFI error: module '%s' does not export paw_library_info\n", resolved);
        dlFreeLibrary(handle);
        free(resolved);
        return NULL;
    }

    paw_library_info_fn info_fn = (paw_library_info_fn)info_symbol;
    const paw_library_t *info = info_fn();

    if (!info) {
        fprintf(stderr, "FFI error: module '%s' returned NULL metadata\n", resolved);
        dlFreeLibrary(handle);
        free(resolved);
        return NULL;
    }

    if (info->abi_version != PAW_FFI_ABI_VERSION) {
        fprintf(stderr, "FFI error: module '%s' has incompatible Paw FFI ABI\n", resolved);
        fprintf(stderr, "  module ABI: %u\n", info->abi_version);
        fprintf(stderr, "  runtime ABI: %u\n", PAW_FFI_ABI_VERSION);
        dlFreeLibrary(handle);
        free(resolved);
        return NULL;
    }

    if (!info->functions && info->function_count != 0) {
        fprintf(stderr, "FFI error: module '%s' has a NULL function table\n", resolved);
        dlFreeLibrary(handle);
        free(resolved);
        return NULL;
    }

    for (uint32_t i = 0; i < info->function_count; ++i) {
        const paw_ffi_function_t *fn = &info->functions[i];

        if (!fn->name || !*fn->name) {
            fprintf(stderr, "FFI error: module '%s' contains a function with no name\n", resolved);
            dlFreeLibrary(handle);
            free(resolved);
            return NULL;
        }

        if (!fn->address) {
            fprintf(stderr, "FFI error: module '%s' function '%s' has a NULL address\n", resolved, fn->name);
            dlFreeLibrary(handle);
            free(resolved);
            return NULL;
        }

        if (fn->arg_count > 15) {
            fprintf(stderr, "FFI error: module '%s' function '%s' has %u arguments; Paw supports at most 15\n", resolved, fn->name, fn->arg_count);
            dlFreeLibrary(handle);
            free(resolved);
            return NULL;
        }

        if (!ffi_type_valid((paw_ffi_type_t)fn->return_type)) {
            fprintf(stderr, "FFI error: module '%s' function '%s' has invalid return type %u\n", resolved, fn->name, fn->return_type);
            dlFreeLibrary(handle);
            free(resolved);
            return NULL;
        }

        for (uint32_t a = 0; a < fn->arg_count; ++a) {
            if (!ffi_type_valid((paw_ffi_type_t)fn->args[a]) || fn->args[a] == PAW_FFI_VOID) {
                fprintf(stderr, "FFI error: module '%s' function '%s' has invalid argument type %u at argument %u\n", resolved, fn->name, fn->args[a], a + 1);
                dlFreeLibrary(handle);
                free(resolved);
                return NULL;
            }
        }
    }

    paw_loaded_library_t *lib = &g_libraries[g_library_count++];

    lib->requested_name = strdup(requested);
    lib->resolved_path = resolved;
    lib->handle = handle;
    lib->info = info;

    if (!lib->requested_name) {
        dlFreeLibrary(handle);
        free(lib->resolved_path);
        g_library_count--;
        return NULL;
    }

    return lib;
}

static const paw_ffi_function_t *find_function(paw_loaded_library_t *library, const char *name) {
    if (!library || !library->info || !name) return NULL;

    for (uint32_t i = 0; i < library->info->function_count; ++i) {
        const paw_ffi_function_t *fn = &library->info->functions[i];

        if (fn->name && strcmp(fn->name, name) == 0) return fn;
    }

    return NULL;
}

static uint32_t register_runtime_function(paw_loaded_library_t *library, const paw_ffi_function_t *function) {
    for (size_t i = 0; i < g_function_count; ++i) {
        if (g_functions[i].library == library && g_functions[i].function == function) return (uint32_t)(i + 1);
    }

    if (g_function_count >= MAX_PAW_FUNCTIONS) return 0;

    g_functions[g_function_count].library = library;
    g_functions[g_function_count].function = function;
    g_function_count++;

    return (uint32_t)g_function_count;
}

static paw_runtime_function_t *get_runtime_function(uint32_t id) {
    if (id == 0) return NULL;

    size_t index = (size_t)(id - 1);

    if (index >= g_function_count) return NULL;

    return &g_functions[index];
}

static void ffi_push_argument(VM *vm, paw_ffi_type_t type, uintptr_t value) {
    switch (type) {
        case PAW_FFI_INT:
            dcArgInt(g_vm_ffi, (DCint)(int32_t)value);
            break;

        case PAW_FFI_CHAR:
            dcArgChar(g_vm_ffi, (DCchar)value);
            break;

        case PAW_FFI_CSTRING: {
            const char *str = VM_get_string(vm, (u32)value);
            dcArgPointer(g_vm_ffi, (DCpointer)str);
            break;
        }

        case PAW_FFI_POINTER:
            dcArgPointer(g_vm_ffi, (DCpointer)value);
            break;

        case PAW_FFI_VOID:
        default:
            break;
    }
}

static uintptr_t ffi_call(VM *vm, const paw_ffi_function_t *fn) {
    if (!fn || !fn->address) return 0;

    g_last_ffi_type = (paw_ffi_type_t)fn->return_type;
    g_last_ffi_value = 0;
    g_last_ffi_valid = 0;

    dcReset(g_vm_ffi);

    for (uint32_t i = 0; i < fn->arg_count; ++i) {
        if (i >= 15) {
            fprintf(stderr, "FFI error: '%s' has too many arguments\n", fn->name ? fn->name : "?");
            return 0;
        }

        ffi_push_argument(vm, (paw_ffi_type_t)fn->args[i], vm->regs[i + 1]);
    }

    uintptr_t result = 0;

    switch ((paw_ffi_type_t)fn->return_type) {
        case PAW_FFI_VOID:
            dcCallVoid(g_vm_ffi, fn->address);
            result = 0;
            break;

        case PAW_FFI_CHAR:
            result = (uintptr_t)(uint8_t)dcCallChar(g_vm_ffi, fn->address);
            break;

        case PAW_FFI_INT:
            result = (uintptr_t)(int32_t)dcCallInt(g_vm_ffi, fn->address);
            break;

        case PAW_FFI_POINTER:
            result = (uintptr_t)dcCallPointer(g_vm_ffi, fn->address);
            break;

        case PAW_FFI_CSTRING: {
            const char *result_string = (const char *)dcCallPointer(g_vm_ffi, fn->address);

            if (!result_string) {
                result = 0;
                break;
            }

            int id = VM_register_str(vm, result_string);

            if (id < 0) {
                fprintf(stderr, "FFI error: failed to register C-string return value from '%s'\n", fn->name ? fn->name : "?");
                result = 0;
                break;
            }

            result = (uintptr_t)id;
            break;
        }

        default:
            fprintf(stderr, "FFI error: unsupported return type %u\n", fn->return_type);
            return 0;
    }

    g_last_ffi_value = result;
    g_last_ffi_valid = 1;

    return result;
}

static int unpack_ffi_types(VM *vm, uint32_t *argc, uint64_t *types) {
    if (!vm || !argc || !types) return 0;
    if (vm->SP + 3 > MAX_STACK_SIZE) return 0;

    uint32_t high = vm->stack[vm->SP++];
    uint32_t low  = vm->stack[vm->SP++];
    *argc          = vm->stack[vm->SP++];

    *types = (uint64_t)low | ((uint64_t)high << 32);

    return 1;
}

static paw_ffi_type_t unpack_ffi_type(uint64_t packed, uint32_t index) {
    return (paw_ffi_type_t)((packed >> (index * 3)) & 0x7);
}

static void ffi_error_call(paw_runtime_function_t *runtime_fn, const char *message) {
    if (!runtime_fn || !runtime_fn->library || !runtime_fn->function) {
        fprintf(stderr, "FFI error: %s\n", message);
        return;
    }

    fprintf(stderr, "FFI error: %s.%s()\n", runtime_fn->library->info && runtime_fn->library->info->name ? runtime_fn->library->info->name : runtime_fn->library->requested_name, runtime_fn->function->name ? runtime_fn->function->name : "?");
    fprintf(stderr, "  %s\n", message);
}

static int ffi_check_types(VM *vm, paw_runtime_function_t *runtime_fn) {
    if (!vm || !runtime_fn || !runtime_fn->function) return 0;

    uint32_t argc = 0;
    uint64_t packed = 0;

    if (!unpack_ffi_types(vm, &argc, &packed)) {
        fprintf(stderr, "FFI error: malformed type-check frame\n");
        return 0;
    }

    const paw_ffi_function_t *fn = runtime_fn->function;

    if (argc != fn->arg_count) {
        char message[128];
        snprintf(message, sizeof(message), "expected %u arguments, got %u", fn->arg_count, argc);
        ffi_error_call(runtime_fn, message);
        return 0;
    }

    for (uint32_t i = 0; i < argc; ++i) {
        paw_ffi_type_t actual = unpack_ffi_type(packed, i);
        paw_ffi_type_t expected = (paw_ffi_type_t)fn->args[i];

        if (actual == PAW_FFI_UNKNOWN) continue;

        if (actual != expected) {
            char message[192];
            snprintf(message, sizeof(message), "argument %u: expected %s, got %s", i + 1, ffi_type_name(expected), ffi_type_name(actual));
            ffi_error_call(runtime_fn, message);
            return 0;
        }
    }

    return 1;
}

static void custom_syscalls(VM *vm, Memory *mem, u32 sys_code) {
    switch (sys_code) {
        case PAW_SYS_PUTCHAR:
            putchar((char)vm->regs[0]);
            break;

        case PAW_SYS_PRINT_STRING: {
            const char *str = VM_get_string(vm, vm->regs[0]);

            if (str) fputs(str, stdout);

            break;
        }

        case PAW_SYS_PRINTF: {
            const char *fmt = VM_get_string(vm, vm->regs[0]);

            if (fmt) VM_printf(vm, mem, 1, 15, fmt);

            break;
        }

        case PAW_SYS_FFI_LOOKUP: {
            const char *library_name = VM_get_string(vm, vm->regs[0]);
            const char *symbol_name = VM_get_string(vm, vm->regs[1]);

            if (!library_name || !symbol_name) {
                fprintf(stderr, "FFI error: invalid module or function name\n");
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            paw_loaded_library_t *library = load_paw_library(library_name);

            if (!library) {
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            const paw_ffi_function_t *function = find_function(library, symbol_name);

            if (!function) {
                fprintf(stderr, "FFI error: function '%s' was not found in module '%s'\n", symbol_name, library_name);
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            if (function->arg_count > 15) {
                fprintf(stderr, "FFI error: function '%s' in module '%s' has %u arguments; Paw supports at most 15\n", symbol_name, library_name, function->arg_count);
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            uint32_t function_id = register_runtime_function(library, function);

            if (!function_id) {
                fprintf(stderr, "FFI error: runtime function table exhausted while loading '%s'\n", symbol_name);
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            vm->regs[0] = function_id;
            break;
        }

        case PAW_SYS_FFI_CHECK: {
            uint32_t function_id = (uint32_t)vm->regs[0];
            paw_runtime_function_t *runtime_fn = get_runtime_function(function_id);

            if (!runtime_fn) {
                fprintf(stderr, "FFI error: invalid function ID %u during argument validation\n", function_id);
                vm->is_running = 0;
                break;
            }

            if (!ffi_check_types(vm, runtime_fn)) {
                vm->regs[0] = 0;
                vm->is_running = 0;
            }

            break;
        }

        case PAW_SYS_FFI_CALL: {
            uint32_t function_id = (uint32_t)vm->regs[0];
            paw_runtime_function_t *runtime_fn = get_runtime_function(function_id);

            if (!runtime_fn) {
                fprintf(stderr, "FFI error: invalid function ID %u\n", function_id);
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            vm->regs[0] = (u32)ffi_call(vm, runtime_fn->function);
            break;
        }

        case PAW_SYS_FFI_PRINT: {
            if (!g_last_ffi_valid) break;

            switch (g_last_ffi_type) {
                case PAW_FFI_VOID:
                    break;

                case PAW_FFI_INT:
                    printf("%d\n", (int32_t)g_last_ffi_value);
                    break;

                case PAW_FFI_CHAR:
                    printf("%c\n", (char)g_last_ffi_value);
                    break;

                case PAW_FFI_CSTRING: {
                    const char *str = VM_get_string(vm, (u32)g_last_ffi_value);
                    printf("%s\n", str ? str : "(null)");
                    break;
                }

                case PAW_FFI_POINTER:
                    printf("0x%08X\n", (u32)g_last_ffi_value);
                    break;

                default:
                    fprintf(stderr, "FFI error: cannot print return type %u\n", g_last_ffi_type);
                    break;
            }

            break;
        }

        default:
            fprintf(stderr, "Fault: Unhandled System Call %u\n", sys_code);
            vm->is_running = 0;
            break;
    }
}

static void cleanup_paw_libraries(void) {
    for (size_t i = 0; i < g_library_count; ++i) {
        if (g_libraries[i].handle) dlFreeLibrary(g_libraries[i].handle);
        free(g_libraries[i].requested_name);
        free(g_libraries[i].resolved_path);
    }

    g_library_count = 0;
    g_function_count = 0;
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

    if (!g_vm_ffi) {
        glog_log(NULL, 0, 0, GLOG_FATAL, "Fatal: Failed to create dyncall VM!");
        nu_mm_destroy(g_mm);
        return EXIT_FAILURE;
    }

    dcMode(g_vm_ffi, DC_CALL_C_DEFAULT);

    current_filename = argv[1];
    vm->syscall_handler = custom_syscalls;

    u32 load_vaddr = 0;
    int ret = VM_run_file(current_filename, mem, vm, load_vaddr);

    if (ret != 0) {
        glog_log(NULL, 0, 0, GLOG_FATAL, "Couldn't run bytecode file: %s, errcode: %d", current_filename, ret);
        cleanup_paw_libraries();
        dcFree(g_vm_ffi);
        nu_mm_destroy(g_mm);
        return EXIT_FAILURE;
    }

    VM_clear_strings(vm);
    cleanup_paw_libraries();

    if (g_vm_ffi) dcFree(g_vm_ffi);

    nu_mm_destroy(g_mm);
    return ret;
}
