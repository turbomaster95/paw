#ifndef DOGMA_H
#define DOGMA_H

#include <nu.h>

typedef enum {
    DOGMA_OK = 0,
    DOGMA_ERR_IO,
    DOGMA_ERR_SYNTAX,
    DOGMA_ERR_SEMA,
    DOGMA_ERR_CODEGEN
} dogma_status_t;

dogma_status_t dogma_parse(const char* output_file);
void dogma_ast2file(nu_ast_node_t *node, const char *out_filename);

#endif
