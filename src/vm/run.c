#define GLOG_IMPL
#include <glog.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <nu.h>
#include <vm.h>

nu_mm_t *g_mm = NULL;

char backing[1024 * 1024 * 8]; // 8 mb
char *current_filename = NULL;

int32_t run_bytecode(const char *filename);

int main(int argc, char **argv) {
    glog_init();
    glog_config.show_source = true;
    glog_config.use_color = 1;
    glog_config.prefix = "paw";

    if (argc < 2) {
        glog_log(NULL, 0, 0, GLOG_INFO, "Usage: %s <bytecode.pawv>\n", argv[0]);
        goto fail;
    }

    g_mm = nu_mm_create(NU_MM_ARENA, backing, sizeof(backing));
    if (!g_mm) {
        glog_log(NULL, 0, 0, GLOG_FATAL, "Fatal: Failed to allocate memory arena!");
        goto fail;
    }

    current_filename = argv[1];
    int32_t ran = run_bytecode(current_filename);
    if (ran == -1) {
        glog_log(NULL, 0, 0, GLOG_FATAL, "Couldn't run bytecode!");
	goto fail;
    }

    nu_mm_destroy(g_mm);
    return ran;

fail:
    if (g_mm)  nu_mm_destroy(g_mm);
    return EXIT_FAILURE;
}

int32_t run_bytecode(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    PawHdr hdr;
    if (fread(&hdr, sizeof(PawHdr), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    if (memcmp(hdr.magic, "PAWV", 4) != 0) {
        fprintf(stderr, "Invalid magic header\n");
        fclose(f);
        return -1;
    }

    // Read String Table
    uint32_t str_count = 0;
    fread(&str_count, sizeof(uint32_t), 1, f);
    for (uint32_t i = 0; i < str_count; i++) {
        uint32_t len = 0;
        fread(&len, sizeof(uint32_t), 1, f);
        char *s = nu_alloc(g_mm, len + 1);
        fread(s, sizeof(char), len, f);
        s[len] = '\0';
        vm_register_string(s);
	nu_free(g_mm, s);
    }

    // Read Bytecode
    Instruction *code = nu_alloc(g_mm, sizeof(Instruction) * hdr.inst_count);
    fread(code, sizeof(Instruction), hdr.inst_count, f);
    fclose(f);

    int32_t ret = run_paw_vm(code);
    if (!ret) {
	ret = -1;
    }

    nu_free(g_mm, code);
    return ret;
}
