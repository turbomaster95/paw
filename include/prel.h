#ifndef PREL_H
#define PREL_H

#include <lson.h>

extern LsonTranslator *g_translator;

#define _(String) (g_translator ? lson_tr(g_translator, (String)) : (String))

#endif // PREL_H
