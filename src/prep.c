#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>

#define MAX_DEFINES       2048
#define MAX_COND_DEPTH   128
#define MAX_INCLUDE_PATHS 64
#define MAX_PARAMS        32
#define MAX_ONCE_FILES    256
#define MAX_EXPAND_DEPTH  64

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_init(Buf *b) {
    b->cap = 4096;
    b->data = malloc(b->cap);
    b->data[0] = '\0';
    b->len = 0;
}

static void buf_free(Buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static void buf_grow(Buf *b, size_t need) {
    while (b->len + need + 1 > b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
}

static void buf_append(Buf *b, const char *s) {
    size_t n = strlen(s);
    buf_grow(b, n);
    memcpy(b->data + b->len, s, n + 1);
    b->len += n;
}

static void buf_appendn(Buf *b, const char *s, size_t n) {
    buf_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_appendc(Buf *b, char c) {
    buf_grow(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static char *buf_steal(Buf *b) {
    char *r = b->data;
    b->data = NULL;
    b->len = b->cap = 0;
    return r;
}

typedef struct {
    char  name[256];
    char *value;
    bool  is_func;
    char  params[MAX_PARAMS][64];
    int   num_params;
    bool  variadic;
} Define;

typedef enum {
    COND_IF,
    COND_ELSE,
    COND_DONE
} CondState;

typedef struct {
    CondState state;
    bool      parent_skipping;
} CondEntry;

typedef struct {
    Buf    lines;
    int    *line_nums;
    const char **files;
    size_t count;
    size_t cap;
} Output;

static Define    defines[MAX_DEFINES];
static int       num_defines = 0;

static CondEntry cond_stack[MAX_COND_DEPTH];
static int       cond_depth = 0;

const char *include_paths[MAX_INCLUDE_PATHS];
int       num_include_paths = 0;

static char *once_files[MAX_ONCE_FILES];
static int   num_once_files = 0;

static int   counter_value = 0;
static int   current_line = 0;
static const char *current_file = NULL;

static char  date_str[32];
static char  time_str[32];

static bool is_id_start(char c) {
    return c == '_' || isalpha((unsigned char)c);
}

static bool is_id_cont(char c) {
    return c == '_' || isalnum((unsigned char)c);
}

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

static char *normalize_path(const char *p) {
    return strdup(p);
}

void init_predefined(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(date_str, sizeof(date_str), "\"%b %d %Y\"", tm);
    strftime(time_str, sizeof(time_str), "\"%H:%M:%S\"", tm);

    Define *d = &defines[num_defines++];
    strcpy(d->name, "__PAW__");
    d->value = strdup("1");
    d->is_func = false;

    d = &defines[num_defines++];
    strcpy(d->name, "PAW_VM");
    d->value = strdup("1");
    d->is_func = false;

    d = &defines[num_defines++];
    strcpy(d->name, "__DATE__");
    d->value = strdup(date_str);
    d->is_func = false;

    d = &defines[num_defines++];
    strcpy(d->name, "__TIME__");
    d->value = strdup(time_str);
    d->is_func = false;
}

static bool is_dynamic_macro(const char *name) {
    return strcmp(name, "__FILE__") == 0 ||
           strcmp(name, "__LINE__") == 0 ||
           strcmp(name, "__COUNTER__") == 0;
}

static char *expand_dynamic_macro(const char *name) {
    if (strcmp(name, "__FILE__") == 0) {
        Buf b;
        buf_init(&b);
        buf_appendc(&b, '"');
        buf_append(&b, current_file ? current_file : "<unknown>");
        buf_appendc(&b, '"');
        return buf_steal(&b);
    }
    if (strcmp(name, "__LINE__") == 0) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%d", current_line);
        return strdup(tmp);
    }
    if (strcmp(name, "__COUNTER__") == 0) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%d", counter_value++);
        return strdup(tmp);
    }
    return NULL;
}

Define *find_define(const char *name) {
    for (int i = num_defines - 1; i >= 0; i--) {
        if (strcmp(defines[i].name, name) == 0) return &defines[i];
    }
    return NULL;
}

void add_define_obj(const char *name, const char *value) {
    for (int i = 0; i < num_defines; i++) {
        if (strcmp(defines[i].name, name) == 0) {
            free(defines[i].value);
            defines[i].value = strdup(value);
            defines[i].is_func = false;
            defines[i].num_params = 0;
            defines[i].variadic = false;
            return;
        }
    }
    if (num_defines >= MAX_DEFINES) {
        fprintf(stderr, "paw: too many defines\n");
        exit(1);
    }
    Define *d = &defines[num_defines++];
    strncpy(d->name, name, 255);
    d->name[255] = '\0';
    d->value = strdup(value);
    d->is_func = false;
    d->num_params = 0;
    d->variadic = false;
}

void add_define_func(const char *name, const char *params_str, const char *body, bool variadic) {
    char params[MAX_PARAMS][64];
    int np = 0;

    if (params_str) {
        const char *p = params_str;
        while (*p && *p != ')') {
            p = skip_ws(p);
            if (*p == ')') break;
            if (*p == '.' && *(p+1) == '.' && *(p+2) == '.') {
                variadic = true;
                break;
            }
            int i = 0;
            while (*p && is_id_cont(*p) && i < 63) {
                params[np][i++] = *p++;
            }
            params[np][i] = '\0';
            if (i > 0) np++;
            p = skip_ws(p);
            if (*p == ',') p++;
        }
    }

    for (int i = 0; i < num_defines; i++) {
        if (strcmp(defines[i].name, name) == 0) {
            free(defines[i].value);
            defines[i].value = strdup(body);
            defines[i].is_func = true;
            defines[i].num_params = np;
            defines[i].variadic = variadic;
            memcpy(defines[i].params, params, sizeof(params));
            return;
        }
    }

    if (num_defines >= MAX_DEFINES) {
        fprintf(stderr, "paw: too many defines\n");
        exit(1);
    }
    Define *d = &defines[num_defines++];
    strncpy(d->name, name, 255);
    d->name[255] = '\0';
    d->value = strdup(body);
    d->is_func = true;
    d->num_params = np;
    d->variadic = variadic;
    memcpy(d->params, params, sizeof(params));
}

void remove_define(const char *name) {
    for (int i = 0; i < num_defines; i++) {
        if (strcmp(defines[i].name, name) == 0) {
            free(defines[i].value);
            defines[i] = defines[--num_defines];
            return;
        }
    }
}

static bool is_skipping(void) {
    if (cond_depth == 0) return false;
    return cond_stack[cond_depth - 1].parent_skipping ||
           cond_stack[cond_depth - 1].state == COND_DONE;
}

static bool is_active(void) {
    if (cond_depth == 0) return true;
    CondEntry *e = &cond_stack[cond_depth - 1];
    if (e->parent_skipping) return false;
    return e->state == COND_IF || e->state == COND_ELSE;
}

static void cond_push_if(bool val) {
    if (cond_depth >= MAX_COND_DEPTH) {
        fprintf(stderr, "paw: too many nested #if blocks\n");
        exit(1);
    }
    CondEntry *e = &cond_stack[cond_depth++];
    e->parent_skipping = (cond_depth > 1) ? is_skipping() : false;
    if (e->parent_skipping) {
        e->state = COND_DONE;
    } else {
        e->state = val ? COND_IF : COND_DONE;
    }
}

static void cond_elif(bool val) {
    if (cond_depth == 0) {
        fprintf(stderr, "paw: #elif without #if\n");
        exit(1);
    }
    CondEntry *e = &cond_stack[cond_depth - 1];
    if (e->parent_skipping) return;
    if (e->state == COND_IF) {
        e->state = COND_DONE;
    } else if (e->state == COND_DONE) {
        if (val) e->state = COND_ELSE;
    } else {
        e->state = COND_DONE;
    }
}

static void cond_else(void) {
    if (cond_depth == 0) {
        fprintf(stderr, "paw: #else without #if\n");
        exit(1);
    }
    CondEntry *e = &cond_stack[cond_depth - 1];
    if (e->parent_skipping) return;
    if (e->state == COND_IF) {
        e->state = COND_DONE;
    } else if (e->state == COND_DONE) {
        e->state = COND_ELSE;
    } else {
        e->state = COND_DONE;
    }
}

static void cond_endif(void) {
    if (cond_depth == 0) {
        fprintf(stderr, "paw: #endif without #if\n");
        exit(1);
    }
    cond_depth--;
}

static long eval_expr(const char **pp);

static long eval_primary(const char **pp) {
    const char *p = skip_ws(*pp);
    if (*p == '(') {
        p++;
        long val = eval_expr(&p);
        p = skip_ws(p);
        if (*p == ')') p++;
        *pp = p;
        return val;
    }
    if (isdigit((unsigned char)*p)) {
        long val = 0;
        while (isdigit((unsigned char)*p)) {
            val = val * 10 + (*p - '0');
            p++;
        }
        *pp = p;
        return val;
    }
    if (is_id_start(*p)) {
        char ident[256];
        int i = 0;
        while (is_id_cont(*p) && i < 255) ident[i++] = *p++;
        ident[i] = '\0';
        *pp = p;

        if (strcmp(ident, "defined") == 0) {
            p = skip_ws(*pp);
            bool has_paren = false;
            if (*p == '(') { has_paren = true; p++; }
            p = skip_ws(p);
            char target[256];
            int ti = 0;
            while (*p && is_id_cont(*p) && ti < 255) target[ti++] = *p++;
            target[ti] = '\0';
            p = skip_ws(p);
            if (has_paren && *p == ')') p++;
            *pp = p;
            return find_define(target) ? 1 : 0;
        }

        Define *d = find_define(ident);
        if (d && !d->is_func) {
            return atol(d->value);
        }
        return 0;
    }
    return 0;
}

static long eval_unary(const char **pp) {
    const char *p = skip_ws(*pp);
    if (*p == '!') { p++; *pp = p; return !eval_unary(pp); }
    if (*p == '-') { p++; *pp = p; return -eval_unary(pp); }
    if (*p == '+') { p++; *pp = p; return eval_unary(pp); }
    return eval_primary(pp);
}

static long eval_mul(const char **pp) {
    long val = eval_unary(pp);
    while (true) {
        const char *p = skip_ws(*pp);
        if (*p == '*') { p++; *pp = p; val *= eval_unary(pp); }
        else if (*p == '/') {
            p++; *pp = p;
            long r = eval_unary(pp);
            val = r ? val / r : 0;
        }
        else break;
    }
    return val;
}

static long eval_add(const char **pp) {
    long val = eval_mul(pp);
    while (true) {
        const char *p = skip_ws(*pp);
        if (*p == '+') { p++; *pp = p; val += eval_mul(pp); }
        else if (*p == '-') { p++; *pp = p; val -= eval_mul(pp); }
        else break;
    }
    return val;
}

static long eval_rel(const char **pp) {
    long val = eval_add(pp);
    while (true) {
        const char *p = skip_ws(*pp);
        if (p[0] == '<' && p[1] == '=') { *pp = p + 2; val = (val <= eval_add(pp)); }
        else if (p[0] == '>' && p[1] == '=') { *pp = p + 2; val = (val >= eval_add(pp)); }
        else if (*p == '<') { (*pp)++; val = (val < eval_add(pp)); }
        else if (*p == '>') { (*pp)++; val = (val > eval_add(pp)); }
        else break;
    }
    return val;
}

static long eval_eq(const char **pp) {
    long val = eval_rel(pp);
    while (true) {
        const char *p = skip_ws(*pp);
        if (p[0] == '=' && p[1] == '=') { *pp = p + 2; val = (val == eval_rel(pp)); }
        else if (p[0] == '!' && p[1] == '=') { *pp = p + 2; val = (val != eval_rel(pp)); }
        else break;
    }
    return val;
}

static long eval_and(const char **pp) {
    long val = eval_eq(pp);
    while (true) {
        const char *p = skip_ws(*pp);
        if (p[0] == '&' && p[1] == '&') { *pp = p + 2; val = val && eval_eq(pp); }
        else break;
    }
    return val;
}

static long eval_or(const char **pp) {
    long val = eval_and(pp);
    while (true) {
        const char *p = skip_ws(*pp);
        if (p[0] == '|' && p[1] == '|') { *pp = p + 2; val = val || eval_and(pp); }
        else break;
    }
    return val;
}

static long eval_expr(const char **pp) {
    return eval_or(pp);
}

static long eval_const_expr(const char *expr) {
    const char *p = expr;
    return eval_expr(&p);
}

static char *strip_comments(const char *src) {
    Buf b;
    buf_init(&b);
    bool in_block = false;
    bool in_string = false;

    for (size_t i = 0; src[i]; i++) {
        char c = src[i];
        char next = src[i + 1];

        if (in_block) {
            if (c == '*' && next == '/') { in_block = false; i++; }
            continue;
        }
        if (in_string) {
            buf_appendc(&b, c);
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; buf_appendc(&b, c); continue; }
        if (c == '/' && next == '*') { in_block = true; i++; continue; }
        if (c == '/' && next == '/') {
            while (src[i] && src[i] != '\n') i++;
            if (src[i]) buf_appendc(&b, '\n');
            continue;
        }
        buf_appendc(&b, c);
    }
    return buf_steal(&b);
}

static char *join_continuations(const char *src) {
    Buf b;
    buf_init(&b);
    for (size_t i = 0; src[i]; i++) {
        if (src[i] == '\\' && src[i + 1] == '\n') {
            i++;
            continue;
        }
        buf_appendc(&b, src[i]);
    }
    return buf_steal(&b);
}

static char *expand_macros(const char *input, int depth);

static char *substitute_func_body(const Define *d, char **args, int arg_count) {
    Buf result;
    buf_init(&result);
    const char *p = d->value;

    while (*p) {
        if (is_id_start(*p)) {
            char name[256];
            int i = 0;
            while (is_id_cont(*p) && i < 255) name[i++] = *p++;
            name[i] = '\0';

            int found_idx = -1;
            for (int k = 0; k < d->num_params; k++) {
                if (strcmp(d->params[k], name) == 0) {
                    found_idx = k;
                    break;
                }
            }

            if (found_idx != -1 && found_idx < arg_count) {
                buf_append(&result, args[found_idx]);
            } else {
                buf_append(&result, name);
            }
            continue;
        }
        buf_appendc(&result, *p);
        p++;
    }
    return buf_steal(&result);
}

static char *expand_macros(const char *input, int depth) {
    if (depth > MAX_EXPAND_DEPTH) return strdup(input);

    Buf result;
    buf_init(&result);
    const char *p = input;

    while (*p) {
        if (is_id_start(*p)) {
            int i = 0;
            char name[256];
            while (is_id_cont(*p) && i < 255) name[i++] = *p++;
            name[i] = '\0';

            if (is_dynamic_macro(name)) {
                char *expanded = expand_dynamic_macro(name);
                buf_append(&result, expanded);
                free(expanded);
                continue;
            }

            Define *d = find_define(name);
            if (!d) {
                buf_append(&result, name);
                continue;
            }

            if (!d->is_func) {
                char *saved = d->value;
                d->value = strdup("");
                char *expanded = expand_macros(saved, depth + 1);
                d->value = saved;
                buf_append(&result, expanded);
                free(expanded);
            } else {
                const char *scan = skip_ws(p);
                if (*scan == '(') {
                    scan++;
                    char *args[MAX_PARAMS];
                    int arg_count = 0;
                    for (int k = 0; k < MAX_PARAMS; k++) args[k] = NULL;

                    while (*scan && *scan != ')') {
                        scan = skip_ws(scan);
                        if (*scan == ')') break;

                        Buf arg_buf;
                        buf_init(&arg_buf);
                        int paren_lvl = 0;

                        while (*scan && (*scan != ',' || paren_lvl > 0) && (*scan != ')' || paren_lvl > 0)) {
                            if (*scan == '(') paren_lvl++;
                            else if (*scan == ')') paren_lvl--;
                            buf_appendc(&arg_buf, *scan);
                            scan++;
                        }
                        if (arg_count < MAX_PARAMS) {
                            char *raw_arg = buf_steal(&arg_buf);
                            args[arg_count++] = expand_macros(raw_arg, depth + 1);
                            free(raw_arg);
                        } else {
                            buf_free(&arg_buf);
                        }

                        if (*scan == ',') scan++;
                    }
                    if (*scan == ')') scan++;
                    p = scan;

                    char *subbed = substitute_func_body(d, args, arg_count);
                    char *expanded = expand_macros(subbed, depth + 1);
                    buf_append(&result, expanded);
                    free(subbed);
                    free(expanded);

                    for (int k = 0; k < arg_count; k++) free(args[k]);
                } else {
                    buf_append(&result, name);
                }
            }
            continue;
        }
        buf_appendc(&result, *p);
        p++;
    }
    return buf_steal(&result);
}

static char *resolve_include(const char *fname, bool quoted, const char *current_dir) {
    char path[2048];
    if (quoted && current_dir) {
        snprintf(path, sizeof(path), "%s/%s", current_dir, fname);
        if (read_file(path)) return strdup(path);
    }
    if (read_file(fname)) return strdup(fname);
    for (int i = 0; i < num_include_paths; i++) {
        snprintf(path, sizeof(path), "%s/%s", include_paths[i], fname);
        if (read_file(path)) return strdup(path);
    }
    return NULL;
}

static char *get_dir(const char *filepath) {
    const char *last_slash = strrchr(filepath, '/');
    if (!last_slash) return strdup(".");
    size_t len = last_slash - filepath;
    char *dir = malloc(len + 1);
    memcpy(dir, filepath, len);
    dir[len] = '\0';
    return dir;
}

void out_init(Output *o) {
    buf_init(&o->lines);
    o->cap = 4096;
    o->line_nums = malloc(o->cap * sizeof(int));
    o->files = malloc(o->cap * sizeof(const char*));
    o->count = 0;
}

void out_free(Output *o) {
    buf_free(&o->lines);
    free(o->line_nums);
    free(o->files);
}

static void out_emit(Output *o, const char *line, int orig_line, const char *file) {
    if (o->count >= o->cap) {
        o->cap *= 2;
        o->line_nums = realloc(o->line_nums, o->cap * sizeof(int));
        o->files = realloc(o->files, o->cap * sizeof(const char*));
    }
    o->line_nums[o->count] = orig_line;
    o->files[o->count] = file;
    buf_append(&o->lines, line);
    o->count++;
}

void process_file(const char *filepath, Output *output);

void process_lines(const char *filepath, const char *content, Output *output) {
    char *clean = strip_comments(content);
    char *joined = join_continuations(clean);
    free(clean);

    char *dir = get_dir(filepath);
    const char *saved_file = current_file;
    current_file = filepath;

    const char *p = joined;
    int line_num = 0;

    while (*p) {
        line_num++;
        current_line = line_num;

        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char *line = malloc(len + 2);
        memcpy(line, p, len);
        line[len] = '\n';
        line[len + 1] = '\0';

        if (eol) p = eol + 1;
        else p += len;

        const char *sp = skip_ws(line);
        if (*sp == '#') {
            sp++;
            char dir_name[64];
            int di = 0;
            while (*sp && !isspace((unsigned char)*sp) && di < 63) dir_name[di++] = *sp++;
            dir_name[di] = '\0';
            const char *body = skip_ws(sp);

            if (strcmp(dir_name, "ifdef") == 0) {
                char name[256];
                sscanf(body, "%255s", name);
                cond_push_if(find_define(name) != NULL);
                free(line);
                continue;
            }
            if (strcmp(dir_name, "ifndef") == 0) {
                char name[256];
                sscanf(body, "%255s", name);
                cond_push_if(find_define(name) == NULL);
                free(line);
                continue;
            }
            if (strcmp(dir_name, "if") == 0) {
                char *expanded = expand_macros(body, 0);
                long val = eval_const_expr(expanded);
                free(expanded);
                cond_push_if(val != 0);
                free(line);
                continue;
            }
            if (strcmp(dir_name, "elif") == 0) {
                char *expanded = expand_macros(body, 0);
                long val = eval_const_expr(expanded);
                free(expanded);
                cond_elif(val != 0);
                free(line);
                continue;
            }
            if (strcmp(dir_name, "else") == 0) {
                cond_else();
                free(line);
                continue;
            }
            if (strcmp(dir_name, "endif") == 0) {
                cond_endif();
                free(line);
                continue;
            }
            if (strcmp(dir_name, "error") == 0) {
                if (is_active()) {
                    fprintf(stderr, "paw: %s:%d: #error: %s\n", current_file, line_num, body);
                    exit(1);
                }
                free(line);
                continue;
            }
            if (strcmp(dir_name, "warning") == 0) {
                if (is_active()) {
                    fprintf(stderr, "paw: %s:%d: #warning: %s\n", current_file, line_num, body);
                }
                free(line);
                continue;
            }

            if (!is_active()) { free(line); continue; }

            if (strcmp(dir_name, "define") == 0) {
                const char *rest = skip_ws(body);
                char name[256];
                int ni = 0;
                while (*rest && is_id_cont(*rest) && ni < 255) name[ni++] = *rest++;
                name[ni] = '\0';

                if (*rest == '(') {
                    rest++;
                    const char *pstart = rest;
                    int paren = 1;
                    while (*rest && paren > 0) {
                        if (*rest == '(') paren++;
                        else if (*rest == ')') paren--;
                        if (paren > 0) rest++;
                    }
                    if (*rest == ')') {
                        char params_str[512];
                        size_t plen = rest - pstart;
                        memcpy(params_str, pstart, plen);
                        params_str[plen] = '\0';
                        rest++;
                        rest = skip_ws(rest);
                        add_define_func(name, params_str, rest, false);
                    }
                } else {
                    rest = skip_ws(rest);
                    add_define_obj(name, rest);
                }
                free(line);
                continue;
            }
            if (strcmp(dir_name, "undef") == 0) {
                char name[256];
                if (sscanf(body, "%255s", name) == 1) remove_define(name);
                free(line);
                continue;
            }
            if (strcmp(dir_name, "include") == 0) {
                char fname[512];
                bool quoted = false;
                const char *inc = skip_ws(body);
                if (*inc == '"') {
                    quoted = true;
                    inc++;
                    int i = 0;
                    while (*inc && *inc != '"' && i < 511) fname[i++] = *inc++;
                    fname[i] = '\0';
                }
                char *resolved = resolve_include(fname, quoted, dir);
                if (!resolved) {
                    fprintf(stderr, "paw: include file not found '%s'\n", fname);
                    free(line);
                    continue;
                }
                process_file(resolved, output);
                free(resolved);
                free(line);
                continue;
            }
        }

        if (!is_active()) { free(line); continue; }

        char *expanded = expand_macros(line, 0);
        out_emit(output, expanded, line_num, current_file);
        free(expanded);
        free(line);
    }

    free(joined);
    free(dir);
    current_file = saved_file;
}

void process_file(const char *filepath, Output *output) {
    char *content = read_file(filepath);
    if (!content) {
        fprintf(stderr, "paw: cannot open file: %s\n", filepath);
        return;
    }
    char marker[2048];
    snprintf(marker, sizeof(marker), "#line 1 \"%s\"\n", filepath);
    out_emit(output, marker, 0, filepath);

    process_lines(filepath, content, output);
    free(content);
}

void handle_cmdline_define(const char *arg) {
    char name[256];
    const char *p = arg;
    int i = 0;
    while (*p && *p != '=' && is_id_cont(*p) && i < 255) name[i++] = *p++;
    name[i] = '\0';
    if (*p == '=') {
        p++;
        add_define_obj(name, p);
    } else {
        add_define_obj(name, "1");
    }
}
