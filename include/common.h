#ifndef COMMON_H
#define COMMON_H

#define MAX_STRINGS 1024

#ifndef NEED_FORMAT

int vm_register_string(const char* s);
int vm_register_format(const char* s);
uint32_t vm_get_string_count(void);
const char* vm_get_string(uint32_t id);
void vm_clear_string_table(void);

#else

const char* g_str_table[MAX_STRINGS];
uint32_t g_str_count = 0;

int vm_register_string(const char* s) {
    if (!s || g_str_count >= MAX_STRINGS) return -1;

    for (uint32_t i = 0; i < g_str_count; i++) {
        if (strcmp(g_str_table[i], s) == 0) {
            return (int)i;
        }
    }

    char *persistent_copy = strdup(s);
    if (!persistent_copy) {
        return -1; // oom
    }

    g_str_table[g_str_count] = persistent_copy;
    return (int)(g_str_count++);
}

int vm_register_format(const char* s) {
    return vm_register_string(s);
}

uint32_t vm_get_string_count(void) {
    return g_str_count;
}

void vm_clear_string_table(void) {
    for (uint32_t i = 0; i < g_str_count; i++) {
        if (g_str_table[i]) {
            free((char*)g_str_table[i]);
            g_str_table[i] = NULL;
        }
    }
    g_str_count = 0;
}

const char* vm_get_string(uint32_t id) {

    if (id >= g_str_count || !g_str_table[id]) {
	return "";
    }

    return g_str_table[id];
}

#endif

#ifndef NEED_BASENAME

const char *get_basename(const char *path);

#else

const char *get_basename(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

#endif

#endif
