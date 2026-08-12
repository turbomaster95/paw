#ifndef ETC_H
#define ETC_H

#include <nu.h>

void parse(const char* output_file);
void walk_ast_to_file(nu_ast_node_t *node, const char *out_filename);

#endif
