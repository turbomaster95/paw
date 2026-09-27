#define _GNU_SOURCE
#define GLOG_IMPL

#include <glog.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <nu.h>
#include <vm.h>

#ifndef WASI
#include <dyncall.h>
#include <dynload.h>
#include <pawffi.h>
#endif

#ifdef PAW_STATIC
#include <pthread.h>
#include <sys/syscall.h>
#include "ipc.h"
#include "pawd_emb.h"
#endif

#include <lson.h>

#define NEED_BASENAME
#define NEED_FORMAT
#include <common.h>

#define DISASM_IMPLEMENTATION
#include <nosry/disasm.h>

nu_mm_t *g_mm = NULL;
char backing[1024 * 1024 * 8];
char *current_filename = NULL;

#ifndef WASI
DCCallVM *g_vm_ffi = NULL;
#endif

LsonTranslator *g_translator = NULL;

#define MAX_PAW_LIBRARIES 64
#define MAX_PAW_FUNCTIONS 512
#define PAW_FFI_UNKNOWN 7

#ifndef WASI

typedef struct {
    char *requested_name;
    char *resolved_path;
#ifndef PAW_STATIC
    DLLib *handle;
#else
    void *handle;
#endif
    const paw_library_t *info;
    paw_ffi_function_t *cloned_functions;
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

#ifdef PAW_STATIC

static int g_pawd_shm_fd = -1;
static SharedIPC *g_pawd_shm = NULL;
static pid_t g_pawd_pid = -1;
static char g_pawd_bin_path[PATH_MAX];

static int ipc_put_u32(unsigned char **ptr, size_t *left, uint32_t value) {
    if (!ptr || !*ptr || !left || *left < sizeof(uint32_t)) return 0;

    memcpy(*ptr, &value, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    *left -= sizeof(uint32_t);

    return 1;
}

static int ipc_put_u64(unsigned char **ptr, size_t *left, uint64_t value) {
    if (!ptr || !*ptr || !left || *left < sizeof(uint64_t)) return 0;

    memcpy(*ptr, &value, sizeof(uint64_t));
    *ptr += sizeof(uint64_t);
    *left -= sizeof(uint64_t);

    return 1;
}

static int ipc_put_bytes(unsigned char **ptr, size_t *left, const void *value, size_t size) {
    if (!ptr || !*ptr || !left || (!value && size != 0) || size > *left) return 0;

    if (size) memcpy(*ptr, value, size);

    *ptr += size;
    *left -= size;

    return 1;
}

static int ipc_get_u32(const unsigned char **ptr, size_t *left, uint32_t *value) {
    if (!ptr || !*ptr || !left || !value || *left < sizeof(uint32_t)) return 0;

    memcpy(value, *ptr, sizeof(uint32_t));
    *ptr += sizeof(uint32_t);
    *left -= sizeof(uint32_t);

    return 1;
}

static int ipc_get_u64(const unsigned char **ptr, size_t *left, uint64_t *value) {
    if (!ptr || !*ptr || !left || !value || *left < sizeof(uint64_t)) return 0;

    memcpy(value, *ptr, sizeof(uint64_t));
    *ptr += sizeof(uint64_t);
    *left -= sizeof(uint64_t);

    return 1;
}

static int ipc_get_bytes(const unsigned char **ptr, size_t *left, const unsigned char **value, size_t size) {
    if (!ptr || !*ptr || !left || !value || size > *left) return 0;

    *value = *ptr;
    *ptr += size;
    *left -= size;

    return 1;
}

static int init_pawd_daemon(void) {
    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    char template[] = "/tmp/pawd_XXXXXX";
    char shm_fd_str[32];
    int bin_fd;
    int rc;
    size_t written = 0;
    size_t binary_size = sizeof(pawd_bin);

    g_pawd_shm_fd = memfd_create("paw_shm", 0);

    if (g_pawd_shm_fd < 0) {
        perror("memfd_create");
        return -1;
    }

    if (ftruncate(g_pawd_shm_fd, sizeof(SharedIPC)) < 0) {
        perror("ftruncate");
        close(g_pawd_shm_fd);
        g_pawd_shm_fd = -1;
        return -1;
    }

    g_pawd_shm = mmap(NULL, sizeof(SharedIPC), PROT_READ | PROT_WRITE, MAP_SHARED, g_pawd_shm_fd, 0);

    if (g_pawd_shm == MAP_FAILED) {
        perror("mmap");
        close(g_pawd_shm_fd);
        g_pawd_shm_fd = -1;
        g_pawd_shm = NULL;
        return -1;
    }

    memset(g_pawd_shm, 0, sizeof(SharedIPC));

    rc = pthread_mutexattr_init(&mutex_attr);

    if (rc != 0) {
        return -1;
    }

    rc = pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

    if (rc != 0) {
        pthread_mutexattr_destroy(&mutex_attr);
        return -1;
    }

    rc = pthread_mutex_init(&g_pawd_shm->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    if (rc != 0) {
        return -1;
    }

    rc = pthread_condattr_init(&cond_attr);

    if (rc != 0) {
        return -1;
    }

    rc = pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);

    if (rc != 0) {
        pthread_condattr_destroy(&cond_attr);
        return -1;
    }

    rc = pthread_cond_init(&g_pawd_shm->cond_host, &cond_attr);

    if (rc != 0) {
        pthread_condattr_destroy(&cond_attr);
        return -1;
    }

    rc = pthread_cond_init(&g_pawd_shm->cond_helper, &cond_attr);
    pthread_condattr_destroy(&cond_attr);

    if (rc != 0) {
        return -1;
    }

    g_pawd_shm->command = CMD_IDLE;
    g_pawd_shm->status = STATUS_IDLE;
    g_pawd_shm->data_size = 0;

    bin_fd = mkstemp(template);

    if (bin_fd < 0) {
        perror("mkstemp");
        return -1;
    }

    snprintf(g_pawd_bin_path, sizeof(g_pawd_bin_path), "%s", template);

    while (written < binary_size) {
        ssize_t n = write(bin_fd, pawd_bin + written, binary_size - written);

        if (n <= 0) {
            close(bin_fd);
            unlink(g_pawd_bin_path);
            g_pawd_bin_path[0] = '\0';
            return -1;
        }

        written += (size_t)n;
    }

    close(bin_fd);

    if (chmod(g_pawd_bin_path, 0755) < 0) {
        unlink(g_pawd_bin_path);
        g_pawd_bin_path[0] = '\0';
        return -1;
    }

    snprintf(shm_fd_str, sizeof(shm_fd_str), "%d", g_pawd_shm_fd);

    g_pawd_pid = fork();

    if (g_pawd_pid < 0) {
        unlink(g_pawd_bin_path);
        g_pawd_bin_path[0] = '\0';
        return -1;
    }

    if (g_pawd_pid == 0) {
        execl(g_pawd_bin_path, "pawd", shm_fd_str, NULL);
        _exit(127);
    }

    return 0;
}

static void stop_pawd_daemon(void) {
    if (!g_pawd_shm) return;

    pthread_mutex_lock(&g_pawd_shm->mutex);

    g_pawd_shm->status = STATUS_IDLE;
    g_pawd_shm->data_size = 0;
    g_pawd_shm->command = CMD_EXIT;

    pthread_cond_signal(&g_pawd_shm->cond_helper);

    while (g_pawd_shm->command != CMD_IDLE) {
        pthread_cond_wait(&g_pawd_shm->cond_host, &g_pawd_shm->mutex);
    }

    pthread_mutex_unlock(&g_pawd_shm->mutex);

    if (g_pawd_pid > 0) waitpid(g_pawd_pid, NULL, 0);

    munmap(g_pawd_shm, sizeof(SharedIPC));
    close(g_pawd_shm_fd);

    g_pawd_shm = NULL;
    g_pawd_shm_fd = -1;
    g_pawd_pid = -1;
}

#endif

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
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-')) return 0;
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

#ifdef PAW_STATIC

static paw_loaded_library_t *load_paw_library(const char *requested) {
    char *resolved = NULL;
    unsigned char *ptr;
    size_t left;
    uint32_t fn_count;
    paw_loaded_library_t *lib;
    paw_library_t *info = NULL;
    paw_ffi_function_t *funcs = NULL;

    if (!requested) return NULL;

    for (size_t i = 0; i < g_library_count; ++i) {
        if (g_libraries[i].requested_name &&
            strcmp(g_libraries[i].requested_name, requested) == 0) {
            return &g_libraries[i];
        }
    }

    if (g_library_count >= MAX_PAW_LIBRARIES) {
        fprintf(stderr, _("FFI error: maximum number of loaded modules (%d) reached\n"), MAX_PAW_LIBRARIES);
        return NULL;
    }

    resolved = resolve_library_path(requested);

    if (!resolved) {
        if (is_paw_library_name(requested)) {
            fprintf(stderr, _("FFI error: module '%s' was not found in the Paw module search paths\n"), requested);
        } else {
            fprintf(stderr, _("FFI error: module '%s' was not found\n"), requested);
        }

        return NULL;
    }

    if (strlen(resolved) + 1 > SHM_DATA_SIZE - sizeof(uint32_t)) {
        fprintf(stderr, _("FFI error: module path is too long\n"));
        free(resolved);
        return NULL;
    }

    pthread_mutex_lock(&g_pawd_shm->mutex);

    g_pawd_shm->command = CMD_LOAD_PLUGIN;
    g_pawd_shm->status = STATUS_IDLE;
    g_pawd_shm->data_size = 0;
    g_pawd_shm->error_buf[0] = '\0';

    ptr = g_pawd_shm->data;
    left = sizeof(g_pawd_shm->data);

    {
        uint32_t path_len = (uint32_t)strlen(resolved) + 1;

        if (!ipc_put_u32(&ptr, &left, path_len) ||
            !ipc_put_bytes(&ptr, &left, resolved, path_len)) {
            pthread_mutex_unlock(&g_pawd_shm->mutex);
            free(resolved);
            return NULL;
        }

        g_pawd_shm->data_size = (uint32_t)(sizeof(g_pawd_shm->data) - left);
    }

    pthread_cond_signal(&g_pawd_shm->cond_helper);

    while (g_pawd_shm->command != CMD_IDLE) {
        pthread_cond_wait(&g_pawd_shm->cond_host, &g_pawd_shm->mutex);
    }

    if (g_pawd_shm->status != STATUS_SUCCESS) {
        fprintf(stderr, _("FFI error: failed to load module '%s': %s\n"), resolved, g_pawd_shm->error_buf);
        pthread_mutex_unlock(&g_pawd_shm->mutex);
        free(resolved);
        return NULL;
    }

    ptr = g_pawd_shm->data;
    left = g_pawd_shm->data_size;

    if (!ipc_get_u32((const unsigned char **)&ptr, &left, &fn_count)) {
        pthread_mutex_unlock(&g_pawd_shm->mutex);
        free(resolved);
        return NULL;
    }

    if (fn_count > MAX_PAW_FUNCTIONS) {
        fprintf(stderr, _("FFI error: module '%s' exports too many functions\n"), resolved);
        pthread_mutex_unlock(&g_pawd_shm->mutex);
        free(resolved);
        return NULL;
    }

    funcs = calloc(fn_count ? fn_count : 1, sizeof(*funcs));
    info = calloc(1, sizeof(*info));

    if (!funcs || !info) {
        free(funcs);
        free(info);
        pthread_mutex_unlock(&g_pawd_shm->mutex);
        free(resolved);
        return NULL;
    }

    for (uint32_t i = 0; i < fn_count; ++i) {
        uint32_t name_len;
        uint32_t return_type;
        uint32_t arg_count;
        uint32_t variadic;
        const unsigned char *name_bytes;

        if (!ipc_get_u32((const unsigned char **)&ptr, &left, &name_len) ||
            name_len == 0 ||
            name_len > left) {
            goto metadata_error;
        }

        if (!ipc_get_bytes((const unsigned char **)&ptr, &left, &name_bytes, name_len)) {
            goto metadata_error;
        }

        if (name_bytes[name_len - 1] != '\0') goto metadata_error;

        funcs[i].name = malloc(name_len);

        if (!funcs[i].name) goto metadata_error;

        memcpy((char *)funcs[i].name, name_bytes, name_len);

        if (!ipc_get_u32((const unsigned char **)&ptr, &left, &return_type) ||
            !ipc_get_u32((const unsigned char **)&ptr, &left, &arg_count)) {
            goto metadata_error;
        }

        if (arg_count > PAW_IPC_MAX_ARGS) goto metadata_error;

        funcs[i].return_type = return_type;
        funcs[i].arg_count = arg_count;
        funcs[i].address = (void *)(uintptr_t)(i + 1);

        for (uint32_t a = 0; a < arg_count; ++a) {
            uint32_t arg_type;

            if (!ipc_get_u32((const unsigned char **)&ptr, &left, &arg_type)) goto metadata_error;

            funcs[i].args[a] = arg_type;
        }

        if (!ipc_get_u32((const unsigned char **)&ptr, &left, &variadic)) goto metadata_error;

        funcs[i].variadic = variadic ? 1 : 0;
    }

    info->abi_version = PAW_FFI_ABI_VERSION;
    info->name = strdup(requested);
    info->function_count = fn_count;
    info->functions = funcs;

    if (!info->name) goto metadata_error;

    lib = &g_libraries[g_library_count];

    memset(lib, 0, sizeof(*lib));

    lib->requested_name = strdup(requested);
    lib->resolved_path = resolved;
    lib->handle = NULL;
    lib->info = info;
    lib->cloned_functions = funcs;

    if (!lib->requested_name) {
        free((void *)info->name);
        free(info);
        free(funcs);
        pthread_mutex_unlock(&g_pawd_shm->mutex);
        free(resolved);
        return NULL;
    }

    g_library_count++;

    pthread_mutex_unlock(&g_pawd_shm->mutex);

    return lib;

metadata_error:
    if (funcs) {
        for (uint32_t i = 0; i < fn_count; ++i) {
            free((void *)funcs[i].name);
        }
    }

    free(funcs);
    free(info);

    pthread_mutex_unlock(&g_pawd_shm->mutex);
    free(resolved);

    fprintf(stderr, _("FFI error: malformed metadata returned by module\n"));
    return NULL;
}

#else

static paw_loaded_library_t *load_paw_library(const char *requested) {
    if (!requested) return NULL;

    for (size_t i = 0; i < g_library_count; ++i) {
        if (strcmp(g_libraries[i].requested_name, requested) == 0) return &g_libraries[i];
    }

    if (g_library_count >= MAX_PAW_LIBRARIES) {
        fprintf(stderr, _("FFI error: maximum number of loaded modules (%d) reached\n"), MAX_PAW_LIBRARIES);
        return NULL;
    }

    char *resolved = resolve_library_path(requested);

    if (!resolved) {
        if (is_paw_library_name(requested)) {
            fprintf(stderr, _("FFI error: module '%s' was not found in the Paw module search paths\n"), requested);
        } else {
            fprintf(stderr, _("FFI error: module '%s' was not found\n"), requested);
        }

        return NULL;
    }

    DLLib *handle = dlLoadLibrary(resolved);

    if (!handle) {
        fprintf(stderr, _("FFI error: failed to load module '%s'\n"), resolved);
        free(resolved);
        return NULL;
    }

    void *info_symbol = dlFindSymbol(handle, "paw_library_info");

    if (!info_symbol) {
        fprintf(stderr, _("FFI error: module '%s' does not export paw_library_info\n"), resolved);
        dlFreeLibrary(handle);
        free(resolved);
        return NULL;
    }

    paw_library_info_fn info_fn = (paw_library_info_fn)info_symbol;
    const paw_library_t *info = info_fn();

    if (!info) {
        fprintf(stderr, _("FFI error: module '%s' returned NULL metadata\n"), resolved);
        dlFreeLibrary(handle);
        free(resolved);
        return NULL;
    }

    if (info->abi_version != PAW_FFI_ABI_VERSION) {
        fprintf(stderr, _("FFI error: module '%s' has incompatible Paw FFI ABI\n"), resolved);
        dlFreeLibrary(handle);
        free(resolved);
        return NULL;
    }

    if (!info->functions && info->function_count != 0) {
        fprintf(stderr, _("FFI error: module '%s' has a NULL function table\n"), resolved);
        dlFreeLibrary(handle);
        free(resolved);
        return NULL;
    }

    for (uint32_t i = 0; i < info->function_count; ++i) {
        const paw_ffi_function_t *fn = &info->functions[i];

        if (!fn->name || !*fn->name || !fn->address) {
            dlFreeLibrary(handle);
            free(resolved);
            return NULL;
        }

        if (fn->arg_count > 15 || !ffi_type_valid((paw_ffi_type_t)fn->return_type)) {
            dlFreeLibrary(handle);
            free(resolved);
            return NULL;
        }

        for (uint32_t a = 0; a < fn->arg_count; ++a) {
            if (!ffi_type_valid((paw_ffi_type_t)fn->args[a]) || fn->args[a] == PAW_FFI_VOID) {
                dlFreeLibrary(handle);
                free(resolved);
                return NULL;
            }
        }
    }

    paw_loaded_library_t *lib = &g_libraries[g_library_count++];

    memset(lib, 0, sizeof(*lib));

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

#endif

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
        if (g_functions[i].library == library && g_functions[i].function == function) {
            return (uint32_t)(i + 1);
        }
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

        default:
            break;
    }
}

static uintptr_t ffi_call(VM *vm, paw_runtime_function_t *runtime_fn) {
    const paw_ffi_function_t *fn;

    if (!runtime_fn || !runtime_fn->function) return 0;

    fn = runtime_fn->function;

    g_last_ffi_type = (paw_ffi_type_t)fn->return_type;
    g_last_ffi_value = 0;
    g_last_ffi_valid = 0;

#ifdef PAW_STATIC

    {
        unsigned char *ptr;
        size_t left;
        uint32_t path_len;
        uint32_t name_len;
        uint32_t arg_count;

        pthread_mutex_lock(&g_pawd_shm->mutex);

        g_pawd_shm->command = CMD_EXEC_FUNC;
        g_pawd_shm->status = STATUS_IDLE;
        g_pawd_shm->data_size = 0;
        g_pawd_shm->error_buf[0] = '\0';

        ptr = g_pawd_shm->data;
        left = sizeof(g_pawd_shm->data);

        path_len = (uint32_t)strlen(runtime_fn->library->resolved_path) + 1;
        name_len = (uint32_t)strlen(fn->name) + 1;
        arg_count = fn->arg_count;

        if (!ipc_put_u32(&ptr, &left, path_len) ||
            !ipc_put_bytes(&ptr, &left, runtime_fn->library->resolved_path, path_len) ||
            !ipc_put_u32(&ptr, &left, name_len) ||
            !ipc_put_bytes(&ptr, &left, fn->name, name_len) ||
            !ipc_put_u32(&ptr, &left, arg_count)) {
            pthread_mutex_unlock(&g_pawd_shm->mutex);
            return 0;
        }

        for (uint32_t i = 0; i < fn->arg_count; ++i) {
            paw_ffi_type_t type = (paw_ffi_type_t)fn->args[i];

            if (!ipc_put_u32(&ptr, &left, type)) {
                pthread_mutex_unlock(&g_pawd_shm->mutex);
                return 0;
            }

            if (type == PAW_FFI_CSTRING) {
                const char *str = VM_get_string(vm, (u32)vm->regs[i + 1]);
                uint32_t string_len = str ? (uint32_t)(strlen(str) + 1) : 0;

                if (!ipc_put_u32(&ptr, &left, string_len)) {
                    pthread_mutex_unlock(&g_pawd_shm->mutex);
                    return 0;
                }

                if (string_len &&
                    !ipc_put_bytes(&ptr, &left, str, string_len)) {
                    pthread_mutex_unlock(&g_pawd_shm->mutex);
                    return 0;
                }
            } else {
                uint64_t value = vm->regs[i + 1];

                if (!ipc_put_u64(&ptr, &left, value)) {
                    pthread_mutex_unlock(&g_pawd_shm->mutex);
                    return 0;
                }
            }
        }

        g_pawd_shm->data_size = (uint32_t)(sizeof(g_pawd_shm->data) - left);

        pthread_cond_signal(&g_pawd_shm->cond_helper);

        while (g_pawd_shm->command != CMD_IDLE) {
            pthread_cond_wait(&g_pawd_shm->cond_host, &g_pawd_shm->mutex);
        }

        if (g_pawd_shm->status != STATUS_SUCCESS) {
            fprintf(stderr, _("FFI error in remote execution of '%s': %s\n"), fn->name, g_pawd_shm->error_buf);
            pthread_mutex_unlock(&g_pawd_shm->mutex);
            return 0;
        }

        ptr = g_pawd_shm->data;
        left = g_pawd_shm->data_size;

        if (fn->return_type == PAW_FFI_CSTRING) {
            uint32_t string_len;
            const unsigned char *string_bytes;
            char *copy;
            int id;

            if (!ipc_get_u32((const unsigned char **)&ptr, &left, &string_len)) {
                pthread_mutex_unlock(&g_pawd_shm->mutex);
                return 0;
            }

            if (string_len == 0) {
                pthread_mutex_unlock(&g_pawd_shm->mutex);
                g_last_ffi_value = 0;
                g_last_ffi_valid = 1;
                return 0;
            }

            if (!ipc_get_bytes((const unsigned char **)&ptr, &left, &string_bytes, string_len) ||
                string_bytes[string_len - 1] != '\0') {
                pthread_mutex_unlock(&g_pawd_shm->mutex);
                return 0;
            }

            copy = malloc(string_len);

            if (!copy) {
                pthread_mutex_unlock(&g_pawd_shm->mutex);
                return 0;
            }

            memcpy(copy, string_bytes, string_len);

            pthread_mutex_unlock(&g_pawd_shm->mutex);

            id = VM_register_str(vm, copy);
            free(copy);

            if (id < 0) return 0;

            g_last_ffi_value = (uintptr_t)id;
            g_last_ffi_valid = 1;

            return (uintptr_t)id;
        }

        {
            uint64_t result;

            if (!ipc_get_u64((const unsigned char **)&ptr, &left, &result)) {
                pthread_mutex_unlock(&g_pawd_shm->mutex);
                return 0;
            }

            pthread_mutex_unlock(&g_pawd_shm->mutex);

            g_last_ffi_value = (uintptr_t)result;
            g_last_ffi_valid = 1;

            return (uintptr_t)result;
        }
    }

#else

    dcReset(g_vm_ffi);

    for (uint32_t i = 0; i < fn->arg_count; ++i) {
        if (i >= 15) {
            fprintf(stderr, _("FFI error: '%s' has too many arguments\n"), fn->name ? fn->name : "?");
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
                fprintf(stderr, _("FFI error: failed to register C-string return value from '%s'\n"), fn->name ? fn->name : "?");
                result = 0;
                break;
            }

            result = (uintptr_t)id;
            break;
        }

        default:
            fprintf(stderr, _("FFI error: unsupported return type %u\n"), fn->return_type);
            return 0;
    }

    g_last_ffi_value = result;
    g_last_ffi_valid = 1;

    return result;

#endif
}

static int unpack_ffi_types(VM *vm, uint32_t *argc, uint64_t *types) {
    if (!vm || !argc || !types) return 0;
    if (vm->SP + 3 > MAX_STACK_SIZE) return 0;

    uint32_t high = vm->stack[vm->SP++];
    uint32_t low = vm->stack[vm->SP++];
    *argc = vm->stack[vm->SP++];

    *types = (uint64_t)low | ((uint64_t)high << 32);

    return 1;
}

static paw_ffi_type_t unpack_ffi_type(uint64_t packed, uint32_t index) {
    return (paw_ffi_type_t)((packed >> (index * 3)) & 0x7);
}

static void ffi_error_call(paw_runtime_function_t *runtime_fn, const char *message) {
    if (!runtime_fn || !runtime_fn->library || !runtime_fn->function) {
        fprintf(stderr, _("FFI error: %s\n"), message);
        return;
    }

    fprintf(stderr, _("FFI error: %s.%s()\n"),
        runtime_fn->library->info && runtime_fn->library->info->name
            ? runtime_fn->library->info->name
            : runtime_fn->library->requested_name,
        runtime_fn->function->name ? runtime_fn->function->name : "?");

    fprintf(stderr, "  %s\n", message);
}

static int ffi_check_types(VM *vm, paw_runtime_function_t *runtime_fn) {
    uint32_t argc = 0;
    uint64_t packed = 0;

    if (!vm || !runtime_fn || !runtime_fn->function) return 0;

    if (!unpack_ffi_types(vm, &argc, &packed)) {
        fprintf(stderr, _("FFI error: malformed type-check frame\n"));
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

            snprintf(message, sizeof(message), "argument %u: expected %s, got %s",
                i + 1,
                ffi_type_name(expected),
                ffi_type_name(actual));

            ffi_error_call(runtime_fn, message);
            return 0;
        }
    }

    return 1;
}

#endif

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

            if (fmt) {
                VM_printf(vm, mem, 1, 15, fmt);
            } else {
                fprintf(stderr, "DEBUG: String was NULL!\n");
            }

            break;
        }

#ifndef WASI
        case PAW_SYS_FFI_LOOKUP: {
            const char *library_name = VM_get_string(vm, vm->regs[0]);
            const char *symbol_name = VM_get_string(vm, vm->regs[1]);

            if (!library_name || !symbol_name) {
                fprintf(stderr, _("FFI error: invalid module or function name\n"));
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
                fprintf(stderr, _("FFI error: function '%s' was not found in module '%s'\n"), symbol_name, library_name);
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            if (function->arg_count > 15) {
                fprintf(stderr, _("FFI error: function '%s' in module '%s' has %u arguments; Paw supports at most 15\n"), symbol_name, library_name, function->arg_count);
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            uint32_t function_id = register_runtime_function(library, function);

            if (!function_id) {
                fprintf(stderr, _("FFI error: runtime function table exhausted while loading '%s'\n"), symbol_name);
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
                fprintf(stderr, _("FFI error: invalid function ID %u during argument validation\n"), function_id);
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
                fprintf(stderr, _("FFI error: invalid function ID %u\n"), function_id);
                vm->regs[0] = 0;
                vm->is_running = 0;
                break;
            }

            vm->regs[0] = (u32)ffi_call(vm, runtime_fn);
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
                    printf("0x%llX\n", (unsigned long long)g_last_ffi_value);
                    break;

                default:
                    fprintf(stderr, _("FFI error: cannot print return type %u\n"), g_last_ffi_type);
                    break;
            }

            break;
        }

#else

        case PAW_SYS_FFI_CHECK:
        case PAW_SYS_FFI_CALL:
        case PAW_SYS_FFI_PRINT:
        case PAW_SYS_FFI_LOOKUP:
            fprintf(stderr, _("FFI: Unavailable in WASI Mode\n"));
            break;

#endif

        default:
            fprintf(stderr, _("Fault: Unhandled System Call %u\n"), sys_code);
            vm->is_running = 0;
            break;
    }
}

#ifndef WASI

static void cleanup_paw_libraries(void) {
    for (size_t i = 0; i < g_library_count; ++i) {
#ifdef PAW_STATIC
        if (g_libraries[i].cloned_functions) {
            uint32_t count = g_libraries[i].info
                ? g_libraries[i].info->function_count
                : 0;

            for (uint32_t j = 0; j < count; ++j) {
                free((void *)g_libraries[i].cloned_functions[j].name);
            }

            free(g_libraries[i].cloned_functions);
        }

        if (g_libraries[i].info) {
            free((void *)g_libraries[i].info->name);
            free((void *)g_libraries[i].info);
        }
#else
        if (g_libraries[i].handle) {
            dlFreeLibrary(g_libraries[i].handle);
        }
#endif

        free(g_libraries[i].requested_name);
        free(g_libraries[i].resolved_path);

        memset(&g_libraries[i], 0, sizeof(g_libraries[i]));
    }

    g_library_count = 0;
    g_function_count = 0;
}

#endif

int main(int argc, char **argv) {
    g_mm = nu_mm_create(NU_MM_ARENA, backing, sizeof(backing));

    if (!g_mm) {
        glog_log(NULL, 0, 0, GLOG_FATAL, _("Fatal: Failed to allocate memory arena!"));
        return EXIT_FAILURE;
    }

    LsonTranslator lson;
    lson_init(&lson, g_mm);

    g_translator = &lson;

    glog_init();
    glog_config.use_color = 1;

    if (argc < 2) {
        glog_log(NULL, 0, 0, GLOG_INFO, _("Usage: %s <bytecode.pawv>\n"), get_basename(argv[0]));
        return EXIT_FAILURE;
    }

    Memory *mem = nu_alloc(g_mm, sizeof(Memory));
    VM *vm = nu_alloc(g_mm, sizeof(VM));

    if (!mem || !vm) {
        glog_log(NULL, 0, 0, GLOG_FATAL, _("Fatal: Out of memory!"));
        nu_mm_destroy(g_mm);
        return EXIT_FAILURE;
    }

    VM_reset(vm, mem);

    u32 load_vaddr = 0;

    if (argc > 2 && strcmp(argv[1], "-d") == 0) {
        FILE *f = fopen(argv[2], "rb");

        if (!f) {
            printf(_("Failed to open file for disassembly: %s\n"), argv[2]);
            return -1;
        }

        size_t prog_len = 0;

        if (VM_import_stream(vm, f, mem, &prog_len, load_vaddr) == 0) {
            printf("Disassembled %s (size: %ld)\n", argv[2], prog_len);
            VM_disassemble_stream(mem->rom, prog_len, stdout);
            fclose(f);
            return 0;
        }

        printf("Failed to parse binary stream: %s\n", argv[2]);
        fclose(f);
        return -1;
    }

#ifndef WASI

#ifndef PAW_STATIC
    g_vm_ffi = dcNewCallVM(4096);

    if (!g_vm_ffi) {
        glog_log(NULL, 0, 0, GLOG_FATAL, _("Fatal: Failed to create dyncall VM!"));
        nu_mm_destroy(g_mm);
        return EXIT_FAILURE;
    }

    dcMode(g_vm_ffi, DC_CALL_C_DEFAULT);

#else

    if (init_pawd_daemon() != 0) {
        glog_log(NULL, 0, 0, GLOG_FATAL, _("Fatal: Failed to start pawd daemon!"));
        nu_mm_destroy(g_mm);
        return EXIT_FAILURE;
    }

#endif

#endif

    current_filename = argv[1];
    vm->syscall_handler = custom_syscalls;

    int ret = VM_run_file(current_filename, mem, vm, load_vaddr);

    if (ret != 0) {
        glog_log(NULL, 0, 0, GLOG_FATAL, _("Couldn't run bytecode file: %s, errcode: %d"), current_filename, ret);

#ifndef WASI

        cleanup_paw_libraries();

#ifdef PAW_STATIC
        stop_pawd_daemon();
#else
        if (g_vm_ffi) dcFree(g_vm_ffi);
#endif

#endif

        nu_mm_destroy(g_mm);
        return EXIT_FAILURE;
    }

    VM_clear_strings(vm);

#ifndef WASI

    cleanup_paw_libraries();

#ifdef PAW_STATIC
    stop_pawd_daemon();
#else
    if (g_vm_ffi) dcFree(g_vm_ffi);
#endif

#endif

    nu_mm_destroy(g_mm);

    return ret;
}
