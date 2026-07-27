#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <comp.h>
#include <etc.h>
#include <nu.h>
#include <glog.h>
#include <vcomp.h>
#include <vm.h>

extern int yylex(void);
extern FILE *yyin;

/* globals */
nu_mm_t *g_mm = NULL;
nu_ast_t *g_ast = NULL;

YYSTYPE yylval;
char *current_filename = NULL;

char backing[1024 * 1024 * 8]; // 8 mb

int main(int argc, char **argv) {
    if (argc < 2) {
        glog_log(NULL, 0, 0, GLOG_INFO, "Usage: %s <source_file> [--dump-bytecode]\n", argv[0]);
        return EXIT_FAILURE;
    }

    bool dump_bytecode = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--dump-bytecode") == 0 || strcmp(argv[i], "-d") == 0) {
            dump_bytecode = true;
        }
    }

    g_mm = nu_mm_create(NU_MM_ARENA, backing, sizeof(backing));
    if (!g_mm) {
        fprintf(stderr, "Fatal: Failed to allocate memory arena.\n");
        return EXIT_FAILURE;
    }

    g_ast = nu_ast_create(g_mm);

    glog_init();
    glog_config.show_source = true;
    glog_config.use_color = 1;
    glog_config.prefix = "paw";

    current_filename = argv[1];
    yyin = fopen(current_filename, "r");
    if (!yyin) {
        glog_log(current_filename, 0, 0, GLOG_FATAL, "Error opening file!");
        goto fail;
    }

    parse();
    fclose(yyin);

    if (!g_ast || !g_ast->root) {
        glog_log(current_filename, 0, 0, GLOG_FATAL, "Parsing failed.");
        goto fail;
    }

    Chunk chunk;
    chunk_init(&chunk);

    if (!compile_ast(g_ast->root, &chunk)) {
        glog_log(current_filename, 0, 0, GLOG_FATAL, "Bytecode compilation failed.");
        chunk_free(&chunk);
        goto fail;
    }

    if (dump_bytecode) {
        printf("\n=== Disassembly: %s ===\n", current_filename);
        disassemble_chunk(&chunk, current_filename);
        printf("=========================\n\n");
    }

    VM vm;
    vm_init(&vm);

    InterpretResult result = vm_run(&vm, &chunk);

    vm_free(&vm);
    chunk_free(&chunk);

    if (result == INTERPRET_RUNTIME_ERROR) {
        glog_log(current_filename, 0, 0, GLOG_ERROR, "Runtime error during VM execution.");
        goto fail;
    }

    nu_ast_destroy(g_ast);
    nu_mm_destroy(g_mm);

    return EXIT_SUCCESS;

fail:
    if (g_ast) nu_ast_destroy(g_ast);
    if (g_mm)  nu_mm_destroy(g_mm);
    return EXIT_FAILURE;
}
