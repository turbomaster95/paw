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

static paw_loaded_library_t
g_libraries[MAX_PAW_LIBRARIES];

static size_t g_library_count = 0;

static paw_runtime_function_t
g_functions[MAX_PAW_FUNCTIONS];

static size_t g_function_count = 0;


int is_paw_library_name(const char *path) {
    if (!path || path[0] != '#') {
        return 0;
    }

    if (path[1] == '\0') {
        return 0;
    }

    for (const char *p = path + 1; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) {
            return 0;
        }
    }

    return 1;
}

static char *resolve_library_path(
    const char *requested
)
{
    if (!requested) {
        return NULL;
    }

    /*
     * Explicit path.
     */
    if (!is_paw_library_name(requested)) {
        return strdup(requested);
    }

    const char *name =
        requested + 1;

    const char *home =
        getenv("HOME");

    if (home && *home) {
        size_t needed =
            strlen(home) +
            strlen("/.local/share/paw/lib/libpaw_") +
            strlen(name) +
            strlen(".so") +
            1;

        char *path =
            malloc(needed);

        if (!path) {
            return NULL;
        }

        snprintf(
            path,
            needed,
            "%s/.local/share/paw/lib/libpaw_%s.so",
            home,
            name
        );

        if (access(path, R_OK) == 0) {
            return path;
        }

        free(path);
    }

    {
        size_t needed =
            strlen("/usr/share/paw/lib/libpaw_") +
            strlen(name) +
            strlen(".so") +
            1;

        char *path =
            malloc(needed);

        if (!path) {
            return NULL;
        }

        snprintf(
            path,
            needed,
            "/usr/share/paw/lib/libpaw_%s.so",
            name
        );

        if (access(path, R_OK) == 0) {
            return path;
        }

        free(path);
    }

    return NULL;
}


/*
 * ------------------------------------------------------------
 * Library loading
 * ------------------------------------------------------------
 */

static paw_loaded_library_t *load_paw_library(
    const char *requested
)
{
    if (!requested) {
        return NULL;
    }

    for (
        size_t i = 0;
        i < g_library_count;
        ++i
    ) {
        if (
            strcmp(
                g_libraries[i].requested_name,
                requested
            ) == 0
        ) {
            return &g_libraries[i];
        }
    }

    if (
        g_library_count >=
        MAX_PAW_LIBRARIES
    ) {
        fprintf(
            stderr,
            "Paw FFI: too many loaded libraries\n"
        );

        return NULL;
    }

    char *resolved =
        resolve_library_path(requested);

    if (!resolved) {
        fprintf(
            stderr,
            "Paw FFI: library '%s' was not found\n",
            requested
        );

        return NULL;
    }

    DLLib *handle =
        dlLoadLibrary(resolved);

    if (!handle) {
        fprintf(
            stderr,
            "Paw FFI: failed to load '%s'\n",
            resolved
        );

        free(resolved);
        return NULL;
    }

    void *info_symbol =
        dlFindSymbol(
            handle,
            "paw_library_info"
        );

    if (!info_symbol) {
        fprintf(
            stderr,
            "Paw FFI: '%s' does not export paw_library_info\n",
            resolved
        );

        dlFreeLibrary(handle);
        free(resolved);

        return NULL;
    }

    paw_library_info_fn info_fn =
        (paw_library_info_fn)info_symbol;

    const paw_library_t *info =
        info_fn();

    if (!info) {
        fprintf(
            stderr,
            "Paw FFI: '%s' returned NULL library metadata\n",
            resolved
        );

        dlFreeLibrary(handle);
        free(resolved);

        return NULL;
    }

    if (
        info->abi_version !=
        PAW_FFI_ABI_VERSION
    ) {
        fprintf(
            stderr,
            "Paw FFI: ABI mismatch in '%s' "
            "(library=%u runtime=%u)\n",
            resolved,
            info->abi_version,
            PAW_FFI_ABI_VERSION
        );

        dlFreeLibrary(handle);
        free(resolved);

        return NULL;
    }

    if (
        !info->functions &&
        info->function_count != 0
    ) {
        fprintf(
            stderr,
            "Paw FFI: malformed function table in '%s'\n",
            resolved
        );

        dlFreeLibrary(handle);
        free(resolved);

        return NULL;
    }

    paw_loaded_library_t *lib =
        &g_libraries[g_library_count++];

    lib->requested_name =
        strdup(requested);

    lib->resolved_path =
        resolved;

    lib->handle =
        handle;

    lib->info =
        info;

    return lib;
}


/*
 * ------------------------------------------------------------
 * Function lookup
 * ------------------------------------------------------------
 */

static const paw_ffi_function_t *find_function(
    paw_loaded_library_t *library,
    const char *name
)
{
    if (!library || !library->info || !name) {
        return NULL;
    }

    for (
        uint32_t i = 0;
        i < library->info->function_count;
        ++i
    ) {
        const paw_ffi_function_t *fn =
            &library->info->functions[i];

        if (
            fn->name &&
            strcmp(fn->name, name) == 0
        ) {
            return fn;
        }
    }

    return NULL;
}

static uint32_t register_runtime_function(
    paw_loaded_library_t *library,
    const paw_ffi_function_t *function
)
{
    for (
        size_t i = 0;
        i < g_function_count;
        ++i
    ) {
        if (
            g_functions[i].library == library &&
            g_functions[i].function == function
        ) {
            return (uint32_t)(i + 1);
        }
    }

    if (
        g_function_count >=
        MAX_PAW_FUNCTIONS
    ) {
        return 0;
    }

    g_functions[g_function_count].library =
        library;

    g_functions[g_function_count].function =
        function;

    g_function_count++;

    return (uint32_t)g_function_count;
}

static paw_runtime_function_t *get_runtime_function(
    uint32_t id
)
{
    if (id == 0) {
        return NULL;
    }

    size_t index =
        (size_t)(id - 1);

    if (index >= g_function_count) {
        return NULL;
    }

    return &g_functions[index];
}


/*
 * ------------------------------------------------------------
 * dyncall argument/return conversion
 * ------------------------------------------------------------
 */

static void ffi_push_argument(
    VM *vm,
    paw_ffi_type_t type,
    uintptr_t value
)
{
    switch (type) {
        case PAW_FFI_INT:
            dcArgInt(
                g_vm_ffi,
                (DCint)value
            );
            break;

        case PAW_FFI_CHAR:
            dcArgChar(
                g_vm_ffi,
                (DCchar)value
            );
            break;

        case PAW_FFI_CSTRING: {
            const char *str =
                VM_get_string(
                    vm,
                    value
                );

            dcArgPointer(
                g_vm_ffi,
                (DCpointer)str
            );
            break;
        }

        case PAW_FFI_POINTER:
            dcArgPointer(
                g_vm_ffi,
                (DCpointer)value
            );
            break;

        case PAW_FFI_VOID:
        default:
            break;
    }
}

static uintptr_t ffi_call(
    VM *vm,
    const paw_ffi_function_t *fn
)
{
    if (
        !fn ||
        !fn->address
    ) {
        return 0;
    }

    dcReset(g_vm_ffi);

    /*
     * Arguments live in R1..R15.
     *
     * R0 contains the runtime function handle.
     */
    for (
        uint32_t i = 0;
        i < fn->arg_count;
        ++i
    ) {
        if (i >= 15) {
            fprintf(
                stderr,
                "Paw FFI: too many arguments\n"
            );

            return 0;
        }

        ffi_push_argument(
            vm,
            (paw_ffi_type_t)fn->args[i],
            vm->regs[i + 1]
        );
    }

    switch (
        (paw_ffi_type_t)fn->return_type
    ) {
        case PAW_FFI_VOID:
            dcCallVoid(
                g_vm_ffi,
                fn->address
            );

            return 0;

        case PAW_FFI_CHAR:
            return (uintptr_t)
                dcCallChar(
                    g_vm_ffi,
                    fn->address
                );

        case PAW_FFI_INT:
            return (uintptr_t)
                dcCallInt(
                    g_vm_ffi,
                    fn->address
                );

        case PAW_FFI_POINTER:
            return (uintptr_t)
                dcCallPointer(
                    g_vm_ffi,
                    fn->address
                );

        case PAW_FFI_CSTRING: {
            const char *result =
                (const char *)dcCallPointer(
                    g_vm_ffi,
                    fn->address
                );

            if (!result) {
                return 0;
            }

            /*
             * Paw values represent strings by VM string IDs,
             * not native pointers.
             */
            int id =
                vm_register_string(result);

            if (id < 0) {
                return 0;
            }

            return (uintptr_t)id;
        }

        default:
            fprintf(
                stderr,
                "Paw FFI: unsupported return type %u\n",
                fn->return_type
            );

            return 0;
    }
}


/*
 * ------------------------------------------------------------
 * VM syscalls
 * ------------------------------------------------------------
 */

static void custom_syscalls(
    VM *vm,
    Memory *mem,
    u32 sys_code
)
{
    switch (sys_code) {
        case 0:
            /*
             * PUTCHAR
             */
            putchar(
                (char)vm->regs[0]
            );
            break;

        case 1: {
            /*
             * PUTS
             */
            const char *str =
                VM_get_string(
                    vm,
                    vm->regs[0]
                );

            if (str) {
                fputs(
                    str,
                    stdout
                );
            }

            break;
        }

        case 2: {
            /*
             * PRINTF
             */
            const char *fmt =
                VM_get_string(
                    vm,
                    vm->regs[0]
                );

            if (fmt) {
                VM_printf(
                    vm,
                    mem,
                    1,
                    15,
                    fmt
                );
            }

            break;
        }

        case 3: {
            /*
             * PAW_FFI_RESOLVE
             *
             * R0 = VM string ID containing:
             *
             *     #entropy
             *
             * or:
             *
             *     ./libpaw_entropy.so
             *
             * R1 = VM string ID containing function name.
             *
             * R0 <- runtime FFI function ID.
             */
            const char *library_name =
                VM_get_string(
                    vm,
                    vm->regs[0]
                );

            const char *symbol_name =
                VM_get_string(
                    vm,
                    vm->regs[1]
                );

            if (
                !library_name ||
                !symbol_name
            ) {
                fprintf(
                    stderr,
                    "Paw FFI: invalid library/function name\n"
                );

                vm->regs[0] = 0;
                break;
            }

            paw_loaded_library_t *library =
                load_paw_library(
                    library_name
                );

            if (!library) {
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            const paw_ffi_function_t *function =
                find_function(
                    library,
                    symbol_name
                );

            if (!function) {
                fprintf(
                    stderr,
                    "Paw FFI: '%s' not exported by '%s'\n",
                    symbol_name,
                    library_name
                );

                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            if (
                function->arg_count >
                15
            ) {
                fprintf(
                    stderr,
                    "Paw FFI: '%s' has too many arguments\n",
                    symbol_name
                );

                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            uint32_t function_id =
                register_runtime_function(
                    library,
                    function
                );

            if (!function_id) {
                fprintf(
                    stderr,
                    "Paw FFI: function table exhausted\n"
                );

                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            vm->regs[0] =
                function_id;

            break;
        }

        case 4: {
            /*
             * PAW_FFI_CALL
             *
             * R0 = runtime function ID
             * R1..R15 = arguments
             *
             * R0 <- result
             */
            uint32_t function_id =
                (uint32_t)vm->regs[0];

            paw_runtime_function_t *runtime_fn =
                get_runtime_function(
                    function_id
                );

            if (!runtime_fn) {
                fprintf(
                    stderr,
                    "Paw FFI: invalid function ID %u\n",
                    function_id
                );

                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            vm->regs[0] =
                ffi_call(
                    vm,
                    runtime_fn->function
                );

            break;
        }

        default:
            fprintf(
                stderr,
                "Fault: Unhandled System Call %u\n",
                sys_code
            );

            vm->is_running = 0;
            break;
    }
}


/*
 * ------------------------------------------------------------
 * Cleanup
 * ------------------------------------------------------------
 */

static void cleanup_paw_libraries(void)
{
    for (
        size_t i = 0;
        i < g_library_count;
        ++i
    ) {
        if (g_libraries[i].handle) {
            dlFreeLibrary(
                g_libraries[i].handle
            );
        }

        free(
            g_libraries[i].requested_name
        );

        free(
            g_libraries[i].resolved_path
        );
    }

    g_library_count = 0;
    g_function_count = 0;
}


/*
 * ------------------------------------------------------------
 * Main
 * ------------------------------------------------------------
 */

int main(
    int argc,
    char **argv
)
{
    glog_init();

    glog_config.use_color = 1;

    if (argc < 2) {
        glog_log(
            NULL,
            0,
            0,
            GLOG_INFO,
            "Usage: %s <bytecode.pawv>\n",
            get_basename(argv[0])
        );

        return EXIT_FAILURE;
    }

    g_mm =
        nu_mm_create(
            NU_MM_ARENA,
            backing,
            sizeof(backing)
        );

    if (!g_mm) {
        glog_log(
            NULL,
            0,
            0,
            GLOG_FATAL,
            "Fatal: Failed to allocate memory arena!"
        );

        return EXIT_FAILURE;
    }

    Memory *mem =
        nu_alloc(
            g_mm,
            sizeof(Memory)
        );

    VM *vm =
        nu_alloc(
            g_mm,
            sizeof(VM)
        );

    if (!mem || !vm) {
        glog_log(
            NULL,
            0,
            0,
            GLOG_FATAL,
            "Fatal: Out of memory!"
        );

        nu_mm_destroy(g_mm);

        return EXIT_FAILURE;
    }

    VM_reset(
        vm,
        mem
    );

    g_vm_ffi =
        dcNewCallVM(4096);

    if (!g_vm_ffi) {
        glog_log(
            NULL,
            0,
            0,
            GLOG_FATAL,
            "Fatal: Failed to create dyncall VM!"
        );

        nu_mm_destroy(g_mm);

        return EXIT_FAILURE;
    }

    dcMode(
        g_vm_ffi,
        DC_CALL_C_DEFAULT
    );

    current_filename =
        argv[1];

    vm->syscall_handler =
        custom_syscalls;

    u32 load_vaddr = 0;

    int ret =
        VM_run_file(
            current_filename,
            mem,
            vm,
            load_vaddr
        );

    if (ret != 0) {
        glog_log(
            NULL,
            0,
            0,
            GLOG_FATAL,
            "Couldn't run bytecode file: %s, errcode: %d",
            current_filename,
            ret
        );

        cleanup_paw_libraries();

        dcFree(
            g_vm_ffi
        );

        nu_mm_destroy(g_mm);

        return EXIT_FAILURE;
    }

    VM_clear_strings(vm);

    cleanup_paw_libraries();

    if (g_vm_ffi) {
        dcFree(
            g_vm_ffi
        );
    }

    nu_mm_destroy(g_mm);

    return ret;
}
