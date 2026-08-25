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

