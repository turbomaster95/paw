#include <comp.h>
#include <nu.h>
#include <nus.h>

extern nu_mm_t* g_mm;
symbt* SymbTable;

symb *symtab_add(symbt *table, const char *name, var_type_t type) {
    if (!table) return NULL;
    symb *sym = nu_alloc(g_mm, sizeof(symb));
    sym->name = nu_strdup(name);
    sym->type = type;
    sym->next = table->head;
    table->head = sym;
    return sym;
}

symb *symtab_lookup(symbt *table, const char *name) {
    if (!table) return NULL;
    for (symb *curr = table->head; curr != NULL; curr = curr->next) {
        if (nu_strcmp(curr->name, name) == 0) {
            return curr;
        }
    }
    return NULL; /* Undeclared variable */
}
