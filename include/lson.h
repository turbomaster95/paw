#ifndef LSON_H
#define LSON_H

// Light-JSON, a smaller json subset just for locale-parsing
// shamelessly ripped off NekoMimiOfficial's Carrot's LSON parser and name :3

#include <nu.h>

typedef struct {
    nu_mm_t *mm;
    nu_map_t *dictionary; // Map of key (char*) -> value (char*)
    char last_error[256];
} LsonTranslator;

void lson_init(LsonTranslator *t, nu_mm_t *mm);
void lson_free(LsonTranslator *t);

bool lson_load_file(LsonTranslator *t, const char *filepath);
bool lson_load_string(LsonTranslator *t, const char *lson_text);

const char* lson_tr(const LsonTranslator *t, const char *key);
const char* lson_get_last_error(const LsonTranslator *t);

#endif // LSON_H
