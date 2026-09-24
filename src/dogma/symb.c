#include <comp.h>
#include <nu.h>
#include <string.h>
#include <stdlib.h>

extern nu_mm_t* g_mm;
symbt* SymbTable;

symb *symtab_add(symbt *table, const char *name, var_type_t type) {
    if (!table || !name) return NULL;

    symb *sym = nu_alloc(g_mm, sizeof(symb));
    if (!sym) return NULL;

    memset(sym, 0, sizeof(symb));
    sym->name = strdup(name);
    if (!sym->name) {
        nu_free(g_mm, sym);
        return NULL;
    }

    sym->type = type;
    sym->next = table->head;
    table->head = sym;

    return sym;
}

symb *symtab_lookup(symbt *table, const char *name) {
    if (!table || !name) return NULL;

    for (symb *curr = table->head; curr != NULL; curr = curr->next) {
        if (curr->name != NULL && strcmp(curr->name, name) == 0) {
            return curr;
        }
    }
    return NULL; /* Undeclared variable */
}
