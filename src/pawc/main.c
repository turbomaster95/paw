#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <comp.h>
#include <dogma.h>
#include <nu.h>
#include <glog.h>
#include <lson.h>

#define NEED_BASENAME
#include <common.h>

#include <prep.c>

extern int yylex(void);
extern FILE *yyin;

/* globals */
nu_mm_t *g_mm = NULL;
nu_ast_t *g_ast = NULL;
LsonTranslator *g_translator = NULL;

char *current_filename = NULL;

char backing[1024 * 1024 * 20]; // 20 mb

char *get_noext_filename(const char *path) {
    if (!path) return NULL;

    const char *base = get_basename(path);
    const char *dot = strrchr(base, '.');

    size_t len = (dot && dot != base) ? (size_t)(dot - base) : strlen(base);

    char *res = nu_alloc(g_mm, len + 1);
    if (!res) return NULL;

    memcpy(res, base, len);
    res[len] = '\0';

    return res;
}

int main(int argc, char **argv) {
    g_mm = nu_mm_create(NU_MM_SLOB, backing, sizeof(backing));
    if (!g_mm) {
        fprintf(stderr, "Fatal: Failed to allocate memory arena.\n");
        return EXIT_FAILURE;
    }

    LsonTranslator lson;
    lson_init(&lson, g_mm);

    g_translator = &lson;

    char *localename = getenv("PLOCALE_FILE");

    localename = (localename == NULL) ? "" : localename;

    if (!lson_load_file(&lson, localename)) {
        fprintf(stderr, "Using Default Locale: %s\n\n", (char*)"English");
    } else {
        printf("Loaded locale: %s", lson_tr(&lson, "MSG_WELCOME"));
    }

    if (argc < 2) {
        glog_log(NULL, 0, 0, GLOG_NOTEXT, _("Usage: %s [options] <source_file>"), get_basename(argv[0]));
	glog_log(NULL, 0, 0, GLOG_NOTEXT, _("Options:"));
	glog_log(NULL, 0, 0, GLOG_NOTEXT, _(" -D<name>       Defines a preprocessor variable"));
	glog_log(NULL, 0, 0, GLOG_NOTEXT, _(" -I<path>       Includes a folder into the global list"));
        goto fail;
    }

    glog_init();
    glog_config.show_source = true;
    glog_config.use_color = 1;
    glog_config.prefix = "paw";

    init_predefined();

    const char *source_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-I", 2) == 0) {
            const char *path = argv[i] + 2;
            if (*path) {
                include_paths[num_include_paths++] = path;
            } else if (i + 1 < argc) {
                include_paths[num_include_paths++] = argv[++i];
            }
        } else if (strncmp(argv[i], "-D", 2) == 0) {
            const char *def = argv[i] + 2;
            if (*def) {
                handle_cmdline_define(def);
            } else if (i + 1 < argc) {
                handle_cmdline_define(argv[++i]);
            }
        } else if (argv[i][0] != '-') {
            source_file = argv[i];
        } else {
            fprintf(stderr, _("paw: unknown option: %s\n"), argv[i]);
            return EXIT_FAILURE;
        }
    }

    if (!source_file) {
        glog_log(NULL, 0, 0, GLOG_FATAL, _("No input source file specified."));
        return EXIT_FAILURE;
    }

    current_filename = strdup(source_file);

    g_ast = nu_ast_create(g_mm);

    // run prep
    Output output;
    out_init(&output);
    process_file(current_filename, &output);

    yyin = fmemopen(output.lines.data, output.lines.len, "r");
    if (!yyin) {
        glog_log(current_filename, 0, 0, GLOG_FATAL, _("Failed to open preprocessor memory stream."));
        out_free(&output);
        goto fail;
    }

    dogma_parse(get_noext_filename(current_filename));
    fclose(yyin);
    out_free(&output);

    if (!g_ast || !g_ast->root) {
        glog_log(current_filename, 0, 0, GLOG_FATAL, _("Parsing failed."));
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

