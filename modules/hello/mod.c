#include <pawffi.h>
#include <stdio.h>

static int add(int a, int b) {
    return a + b;
}

static const char *greet(const char *name) {
    static char buffer[128];

    if (!name) return "Hello, stranger!";

    snprintf(buffer, sizeof(buffer), "Hello, %s!", name);
    return buffer;
}

static void hello(void) {
    puts("hello from C");
}

static const paw_ffi_function_t functions[] = {
    {
        .name = "add",
        .address = (void *)add,
        .return_type = PAW_FFI_INT,
        .arg_count = 2,
        .args = { PAW_FFI_INT, PAW_FFI_INT },
        .variadic = 0
    },
    {
        .name = "greet",
        .address = (void *)greet,
        .return_type = PAW_FFI_CSTRING,
        .arg_count = 1,
        .args = { PAW_FFI_CSTRING },
        .variadic = 0
    },
    {
        .name = "hello",
        .address = (void *)hello,
        .return_type = PAW_FFI_VOID,
        .arg_count = 0,
        .args = { 0 },
        .variadic = 0
    }
};

static const paw_library_t library = {
    .abi_version = PAW_FFI_ABI_VERSION,
    .name = "hello",
    .function_count = sizeof(functions) / sizeof(functions[0]),
    .functions = functions
};

const paw_library_t *paw_library_info(void) {
    return &library;
}
