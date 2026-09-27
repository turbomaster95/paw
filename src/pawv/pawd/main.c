#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <limits.h>

#include <dyncall.h>
#include <pawffi.h>

#include "ipc.h"

#define MAX_LOADED_MODULES 64

typedef struct {
    char *path;
    void *handle;
    const paw_library_t *info;
} LoadedModule;

static LoadedModule g_modules[MAX_LOADED_MODULES];
static size_t g_module_count = 0;
static DCCallVM *g_callvm = NULL;

static void set_error(SharedIPC *shm, const char *fmt, ...) {
    va_list ap;

    shm->status = STATUS_ERROR;

    va_start(ap, fmt);
    vsnprintf(shm->error_buf, sizeof(shm->error_buf), fmt, ap);
    va_end(ap);
}

static void clear_error(SharedIPC *shm) {
    shm->error_buf[0] = '\0';
}

static void finish_command(SharedIPC *shm) {
    shm->command = CMD_IDLE;
    pthread_cond_signal(&shm->cond_host);
}

static int get_u32(const unsigned char **ptr, size_t *left, uint32_t *value) {
    if (!ptr || !*ptr || !left || !value || *left < sizeof(uint32_t)) return 0;

    memcpy(value, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    *left -= sizeof(uint32_t);

    return 1;
}

static int get_u64(const unsigned char **ptr, size_t *left, uint64_t *value) {
    if (!ptr || !*ptr || !left || !value || *left < sizeof(uint64_t)) return 0;

    memcpy(value, *ptr, sizeof(uint64_t));
    *ptr += sizeof(uint64_t);
    *left -= sizeof(uint64_t);

    return 1;
}

static int get_bytes(const unsigned char **ptr, size_t *left, const unsigned char **value, size_t size) {
    if (!ptr || !*ptr || !left || !value || size > *left) return 0;

    *value = *ptr;
    *ptr += size;
    *left -= size;

    return 1;
}

static int put_u32(unsigned char **ptr, size_t *left, uint32_t value) {
    if (!ptr || !*ptr || !left || *left < sizeof(uint32_t)) return 0;

    memcpy(*ptr, &value, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    *left -= sizeof(uint32_t);

    return 1;
}

static int put_u64(unsigned char **ptr, size_t *left, uint64_t value) {
    if (!ptr || !*ptr || !left || *left < sizeof(uint64_t)) return 0;

    memcpy(*ptr, &value, sizeof(uint64_t));
    *ptr += sizeof(uint64_t);
    *left -= sizeof(uint64_t);

    return 1;
}

static int put_bytes(unsigned char **ptr, size_t *left, const void *value, size_t size) {
    if (!ptr || !*ptr || !left || (!value && size != 0) || size > *left) return 0;

    if (size) memcpy(*ptr, value, size);

    *ptr += size;
    *left -= size;

    return 1;
}

static LoadedModule *find_module(const char *path) {
    if (!path) return NULL;

    for (size_t i = 0; i < g_module_count; ++i) {
        if (g_modules[i].path && strcmp(g_modules[i].path, path) == 0) return &g_modules[i];
    }

    return NULL;
}

static LoadedModule *load_module(const char *path, char *error, size_t error_size) {
    LoadedModule *module;
    void *handle;
    void *info_symbol;
    paw_library_info_fn info_fn;
    const paw_library_t *info;

    if (!path || !*path) {
        snprintf(error, error_size, "Empty plugin path");
        return NULL;
    }

    module = find_module(path);

    if (module) return module;

    if (g_module_count >= MAX_LOADED_MODULES) {
        snprintf(error, error_size, "Maximum number of loaded modules reached");
        return NULL;
    }

    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (!handle) {
        const char *dl_error = dlerror();
        snprintf(error, error_size, "%s", dl_error ? dl_error : "dlopen failed");
        return NULL;
    }

    dlerror();
    info_symbol = dlsym(handle, "paw_library_info");

    {
        const char *dl_error = dlerror();

        if (dl_error || !info_symbol) {
            snprintf(error, error_size, "Module does not export paw_library_info: %s", dl_error ? dl_error : "symbol not found");
            dlclose(handle);
            return NULL;
        }
    }

    info_fn = (paw_library_info_fn)info_symbol;
    info = info_fn();

    if (!info) {
        snprintf(error, error_size, "paw_library_info returned NULL");
        dlclose(handle);
        return NULL;
    }

    if (info->abi_version != PAW_FFI_ABI_VERSION) {
        snprintf(error, error_size, "Incompatible Paw FFI ABI: module=%u runtime=%u", info->abi_version, PAW_FFI_ABI_VERSION);
        dlclose(handle);
        return NULL;
    }

    if (info->function_count > 0 && !info->functions) {
        snprintf(error, error_size, "Module has a NULL function table");
        dlclose(handle);
        return NULL;
    }

    for (uint32_t i = 0; i < info->function_count; ++i) {
        const paw_ffi_function_t *fn = &info->functions[i];

        if (!fn->name || !*fn->name) {
            snprintf(error, error_size, "Function %u has no name", i);
            dlclose(handle);
            return NULL;
        }

        if (!fn->address) {
            snprintf(error, error_size, "Function '%s' has a NULL address", fn->name);
            dlclose(handle);
            return NULL;
        }

        if (fn->arg_count > PAW_IPC_MAX_ARGS) {
            snprintf(error, error_size, "Function '%s' has too many arguments", fn->name);
            dlclose(handle);
            return NULL;
        }

        if (fn->return_type != PAW_FFI_VOID &&
            fn->return_type != PAW_FFI_INT &&
            fn->return_type != PAW_FFI_CHAR &&
            fn->return_type != PAW_FFI_CSTRING &&
            fn->return_type != PAW_FFI_POINTER) {
            snprintf(error, error_size, "Function '%s' has invalid return type %u", fn->name, fn->return_type);
            dlclose(handle);
            return NULL;
        }

        for (uint32_t a = 0; a < fn->arg_count; ++a) {
            paw_ffi_type_t type = (paw_ffi_type_t)fn->args[a];

            if (type == PAW_FFI_VOID ||
                (type != PAW_FFI_INT &&
                 type != PAW_FFI_CHAR &&
                 type != PAW_FFI_CSTRING &&
                 type != PAW_FFI_POINTER)) {
                snprintf(error, error_size, "Function '%s' has invalid argument type %u", fn->name, fn->args[a]);
                dlclose(handle);
                return NULL;
            }
        }
    }

    module = &g_modules[g_module_count];

    module->path = strdup(path);

    if (!module->path) {
        snprintf(error, error_size, "Out of memory");
        dlclose(handle);
        return NULL;
    }

    module->handle = handle;
    module->info = info;
    g_module_count++;

    return module;
}

static const paw_ffi_function_t *find_function(LoadedModule *module, const char *name) {
    if (!module || !module->info || !name) return NULL;

    for (uint32_t i = 0; i < module->info->function_count; ++i) {
        const paw_ffi_function_t *fn = &module->info->functions[i];

        if (fn->name && strcmp(fn->name, name) == 0) return fn;
    }

    return NULL;
}

static int serialize_metadata(SharedIPC *shm, const paw_library_t *info) {
    unsigned char *ptr = shm->data;
    size_t left = sizeof(shm->data);

    if (!put_u32(&ptr, &left, info->function_count)) return 0;

    for (uint32_t i = 0; i < info->function_count; ++i) {
        const paw_ffi_function_t *fn = &info->functions[i];
        size_t name_len = strlen(fn->name) + 1;

        if (name_len > UINT32_MAX) return 0;

        if (!put_u32(&ptr, &left, (uint32_t)name_len)) return 0;
        if (!put_bytes(&ptr, &left, fn->name, name_len)) return 0;
        if (!put_u32(&ptr, &left, fn->return_type)) return 0;
        if (!put_u32(&ptr, &left, fn->arg_count)) return 0;

        for (uint32_t a = 0; a < fn->arg_count; ++a) {
            if (!put_u32(&ptr, &left, fn->args[a])) return 0;
        }

        if (!put_u32(&ptr, &left, fn->variadic ? 1 : 0)) return 0;
    }

    shm->data_size = (uint32_t)(sizeof(shm->data) - left);

    return 1;
}

static void handle_load_plugin(SharedIPC *shm) {
    const unsigned char *ptr = shm->data;
    size_t left = shm->data_size;
    uint32_t path_len = 0;
    const unsigned char *path_bytes = NULL;
    char path[PATH_MAX];
    char error[256];
    LoadedModule *module;

    clear_error(shm);

    if (!get_u32(&ptr, &left, &path_len)) {
        set_error(shm, "Malformed LOAD_PLUGIN request");
        return;
    }

    if (path_len == 0 || path_len > sizeof(path)) {
        set_error(shm, "Invalid plugin path length");
        return;
    }

    if (!get_bytes(&ptr, &left, &path_bytes, path_len)) {
        set_error(shm, "Truncated plugin path");
        return;
    }

    if (path_bytes[path_len - 1] != '\0') {
        set_error(shm, "Plugin path is not NUL terminated");
        return;
    }

    memcpy(path, path_bytes, path_len);

    module = load_module(path, error, sizeof(error));

    if (!module) {
        set_error(shm, "%s", error);
        return;
    }

    if (!serialize_metadata(shm, module->info)) {
        set_error(shm, "Failed to serialize module metadata");
        return;
    }

    shm->status = STATUS_SUCCESS;
}

static void handle_exec_func(SharedIPC *shm) {
    const unsigned char *ptr = shm->data;
    size_t left = shm->data_size;
    uint32_t path_len = 0;
    uint32_t symbol_len = 0;
    uint32_t arg_count = 0;
    const unsigned char *path_bytes = NULL;
    const unsigned char *symbol_bytes = NULL;
    char path[PATH_MAX];
    char symbol[256];
    uint64_t args[PAW_IPC_MAX_ARGS];
    paw_ffi_type_t arg_types[PAW_IPC_MAX_ARGS];
    LoadedModule *module;
    const paw_ffi_function_t *fn;
    char error[256];

    clear_error(shm);

    if (!get_u32(&ptr, &left, &path_len)) {
        set_error(shm, "Malformed EXEC_FUNC request");
        return;
    }

    if (path_len == 0 || path_len > sizeof(path)) {
        set_error(shm, "Invalid module path length");
        return;
    }

    if (!get_bytes(&ptr, &left, &path_bytes, path_len)) {
        set_error(shm, "Truncated module path");
        return;
    }

    if (path_bytes[path_len - 1] != '\0') {
        set_error(shm, "Module path is not NUL terminated");
        return;
    }

    memcpy(path, path_bytes, path_len);

    if (!get_u32(&ptr, &left, &symbol_len)) {
        set_error(shm, "Malformed function name");
        return;
    }

    if (symbol_len == 0 || symbol_len > sizeof(symbol)) {
        set_error(shm, "Invalid function name length");
        return;
    }

    if (!get_bytes(&ptr, &left, &symbol_bytes, symbol_len)) {
        set_error(shm, "Truncated function name");
        return;
    }

    if (symbol_bytes[symbol_len - 1] != '\0') {
        set_error(shm, "Function name is not NUL terminated");
        return;
    }

    memcpy(symbol, symbol_bytes, symbol_len);

    if (!get_u32(&ptr, &left, &arg_count)) {
        set_error(shm, "Malformed argument count");
        return;
    }

    if (arg_count > PAW_IPC_MAX_ARGS) {
        set_error(shm, "Too many arguments");
        return;
    }

    module = load_module(path, error, sizeof(error));

    if (!module) {
        set_error(shm, "%s", error);
        return;
    }

    fn = find_function(module, symbol);

    if (!fn) {
        set_error(shm, "Function '%s' not found in Paw metadata", symbol);
        return;
    }

    if (arg_count != fn->arg_count) {
        set_error(shm, "Function '%s' expects %u arguments, got %u", symbol, fn->arg_count, arg_count);
        return;
    }

    for (uint32_t i = 0; i < arg_count; ++i) {
        uint32_t type;

        if (!get_u32(&ptr, &left, &type)) {
            set_error(shm, "Malformed argument %u", i + 1);
            return;
        }

        arg_types[i] = (paw_ffi_type_t)type;

        switch (arg_types[i]) {
            case PAW_FFI_INT:
            case PAW_FFI_CHAR:
            case PAW_FFI_POINTER:
                if (!get_u64(&ptr, &left, &args[i])) {
                    set_error(shm, "Truncated argument %u", i + 1);
                    return;
                }
                break;

            case PAW_FFI_CSTRING: {
                uint32_t string_len;
                const unsigned char *string_bytes;

                if (!get_u32(&ptr, &left, &string_len)) {
                    set_error(shm, "Malformed string argument %u", i + 1);
                    return;
                }

                if (string_len == 0) {
                    args[i] = 0;
                    break;
                }

                if (!get_bytes(&ptr, &left, &string_bytes, string_len)) {
                    set_error(shm, "Truncated string argument %u", i + 1);
                    return;
                }

                if (string_bytes[string_len - 1] != '\0') {
                    set_error(shm, "String argument %u is not NUL terminated", i + 1);
                    return;
                }

                args[i] = (uint64_t)(uintptr_t)string_bytes;
                break;
            }

            default:
                set_error(shm, "Unsupported argument type %u", type);
                return;
        }

        if ((paw_ffi_type_t)fn->args[i] != arg_types[i]) {
            set_error(shm, "Argument %u has type %u, expected %u", i + 1, arg_types[i], fn->args[i]);
            return;
        }
    }

    dcReset(g_callvm);

    for (uint32_t i = 0; i < arg_count; ++i) {
        switch (arg_types[i]) {
            case PAW_FFI_INT:
                dcArgInt(g_callvm, (DCint)(int32_t)args[i]);
                break;

            case PAW_FFI_CHAR:
                dcArgChar(g_callvm, (DCchar)args[i]);
                break;

            case PAW_FFI_CSTRING:
                dcArgPointer(g_callvm, (DCpointer)(uintptr_t)args[i]);
                break;

            case PAW_FFI_POINTER:
                dcArgPointer(g_callvm, (DCpointer)(uintptr_t)args[i]);
                break;

            default:
                set_error(shm, "Unsupported argument type");
                return;
        }
    }

    {
        unsigned char *out = shm->data;
        size_t out_left = sizeof(shm->data);

        switch ((paw_ffi_type_t)fn->return_type) {
            case PAW_FFI_VOID:
                dcCallVoid(g_callvm, fn->address);

                if (!put_u64(&out, &out_left, 0)) {
                    set_error(shm, "Failed to serialize void return");
                    return;
                }
                break;

            case PAW_FFI_INT: {
                int32_t result = dcCallInt(g_callvm, fn->address);

                if (!put_u64(&out, &out_left, (uint64_t)(int64_t)result)) {
                    set_error(shm, "Failed to serialize integer return");
                    return;
                }
                break;
            }

            case PAW_FFI_CHAR: {
                char result = dcCallChar(g_callvm, fn->address);

                if (!put_u64(&out, &out_left, (uint64_t)(uint8_t)result)) {
                    set_error(shm, "Failed to serialize character return");
                    return;
                }
                break;
            }

            case PAW_FFI_POINTER: {
                void *result = dcCallPointer(g_callvm, fn->address);

                if (!put_u64(&out, &out_left, (uint64_t)(uintptr_t)result)) {
                    set_error(shm, "Failed to serialize pointer return");
                    return;
                }
                break;
            }

            case PAW_FFI_CSTRING: {
                const char *result = (const char *)dcCallPointer(g_callvm, fn->address);
                uint32_t length;

                if (!result) {
                    if (!put_u32(&out, &out_left, 0)) {
                        set_error(shm, "Failed to serialize NULL string return");
                        return;
                    }
                    break;
                }

                {
                    size_t length_size = strlen(result) + 1;

                    if (length_size > UINT32_MAX) {
                        set_error(shm, "C-string return is too large");
                        return;
                    }

                    length = (uint32_t)length_size;
                }

                if (!put_u32(&out, &out_left, length) ||
                    !put_bytes(&out, &out_left, result, length)) {
                    set_error(shm, "C-string return is too large for IPC buffer");
                    return;
                }

                break;
            }

            default:
                set_error(shm, "Unsupported return type %u", fn->return_type);
                return;
        }

        shm->data_size = (uint32_t)(sizeof(shm->data) - out_left);
    }

    shm->status = STATUS_SUCCESS;
}

static void cleanup_modules(void) {
    for (size_t i = 0; i < g_module_count; ++i) {
        if (g_modules[i].handle) dlclose(g_modules[i].handle);
        free(g_modules[i].path);
        g_modules[i].path = NULL;
        g_modules[i].handle = NULL;
        g_modules[i].info = NULL;
    }

    g_module_count = 0;
}

int main(int argc, char **argv) {
    SharedIPC *shm;
    int shm_fd;
    DCCallVM *callvm;

    if (argc < 2) return 1;

    shm_fd = atoi(argv[1]);

    shm = mmap(NULL, sizeof(SharedIPC), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (shm == MAP_FAILED) return 1;

    callvm = dcNewCallVM(4096);

    if (!callvm) {
        munmap(shm, sizeof(SharedIPC));
        return 1;
    }

    dcMode(callvm, DC_CALL_C_DEFAULT);
    g_callvm = callvm;

    for (;;) {
        pthread_mutex_lock(&shm->mutex);

        while (shm->command == CMD_IDLE) {
            pthread_cond_wait(&shm->cond_helper, &shm->mutex);
        }

        switch (shm->command) {
            case CMD_LOAD_PLUGIN:
                handle_load_plugin(shm);
                break;

            case CMD_EXEC_FUNC:
                handle_exec_func(shm);
                break;

            case CMD_EXIT:
                shm->status = STATUS_SUCCESS;
                finish_command(shm);
                pthread_mutex_unlock(&shm->mutex);
                dcFree(callvm);
                cleanup_modules();
                munmap(shm, sizeof(SharedIPC));
                return 0;

            default:
                set_error(shm, "Unknown IPC command %d", shm->command);
                break;
        }

        finish_command(shm);
        pthread_mutex_unlock(&shm->mutex);
    }
}
