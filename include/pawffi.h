#ifndef PAW_FFI_H
#define PAW_FFI_H

#include <stdint.h>

#define PAW_FFI_ABI_VERSION 1
#define PAW_FFI_MAX_ARGS    16

typedef enum {
    PAW_FFI_VOID = 0,
    PAW_FFI_INT,
    PAW_FFI_CHAR,
    PAW_FFI_CSTRING,
    PAW_FFI_POINTER
} paw_ffi_type_t;

typedef struct {
    const char *name;

    void *address;

    uint32_t return_type;
    uint32_t arg_count;

    uint8_t args[PAW_FFI_MAX_ARGS];

    uint8_t variadic;
    uint8_t reserved[3];
} paw_ffi_function_t;

typedef struct {
    uint32_t abi_version;

    const char *name;

    uint32_t function_count;

    const paw_ffi_function_t *functions;
} paw_library_t;

typedef const paw_library_t *(*paw_library_info_fn)(void);

#endif
