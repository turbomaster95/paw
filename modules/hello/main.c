#include <pawffi.h>

static const char *hello(void) {
    return "Hello from FFI!";
}

static const paw_ffi_function_t functions[] = {
    {
        .name = "hello",
        .address = (void *)hello,
        .return_type = PAW_FFI_CSTRING,
        .arg_count = 0,
        .args = { 0 },
        .variadic = 0
    }
};

static const paw_library_t library = {
    .abi_version = PAW_FFI_ABI_VERSION,
    .name = "hello",
    .function_count = 1,
    .functions = functions
};

const paw_library_t *paw_library_info(void) {
    return &library;
}
