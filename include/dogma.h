#ifndef ETC_H
#define ETC_H

#include <nu.h>

void dogma_parse(const char* output_file);
void dogma_ast2file(nu_ast_node_t *node, const char *out_filename);

#endif
