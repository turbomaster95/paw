#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>

#define MAX_DEFINES      2048
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

typedef enum {
    TOK_NUM, TOK_IDENT, TOK_OP, TOK_LPAREN, TOK_RPAREN,
    TOK_QUESTION, TOK_COLON, TOK_EOF
} TokType;

typedef struct {
    TokType type;
    long    num;
    char    ident[256];
    char    op[4];
} Token;

typedef struct {
    char  name[256];
    char *value;              /* body text (malloc'd) */
    bool  is_func;
    char  params[MAX_PARAMS][64];
    int   num_params;
    bool  variadic;           /* has ... */
    char *value_tokens;       /* tokenized body for ## handling */
} Define;

typedef enum {
    COND_IF,       /* currently in true branch */
    COND_ELSE,     /* currently in else (could be true or false) */
    COND_DONE      /* already had a true branch, skipping rest */
} CondState;

typedef struct {
    CondState state;
    bool      parent_skipping;  /* was parent already skipping? */
} CondEntry;

static Define    defines[MAX_DEFINES];
static int       num_defines = 0;

static CondEntry cond_stack[MAX_COND_DEPTH];
static int       cond_depth = 0;

static const char *include_paths[MAX_INCLUDE_PATHS];
static int       num_include_paths = 0;

static char *once_files[MAX_ONCE_FILES];   /* normalized paths */
static int   num_once_files = 0;

static int   counter_value = 0;            /* __COUNTER__ */
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

static const char *skip_ws_nl(const char *p) {
    while (*p && (*p == ' ' || *p == '\t')) p++;
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

static bool is_once_file(const char *path) {
    char *norm = normalize_path(path);
    for (int i = 0; i < num_once_files; i++) {
        if (strcmp(once_files[i], norm) == 0) {
            free(norm);
            return true;
        }
    }
    free(norm);
    return false;
}

static void add_once_file(const char *path) {
    if (num_once_files < MAX_ONCE_FILES) {
        once_files[num_once_files++] = normalize_path(path);
    }
}

static void init_predefined(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(date_str, sizeof(date_str), "\"%b %d %Y\"", tm);
    strftime(time_str, sizeof(time_str), "\"%H:%M:%S\"", tm);

    /* __STDC__ */
    Define *d = &defines[num_defines++];
    strcpy(d->name, "__STDC__");
    d->value = strdup("1");
    d->is_func = false;
    d->num_params = 0;
    d->variadic = false;

    /* __STDC_VERSION__ */
    d = &defines[num_defines++];
    strcpy(d->name, "__STDC_VERSION__");
    d->value = strdup("199901L");
    d->is_func = false;
    d->num_params = 0;
    d->variadic = false;

    /* __DATE__ */
    d = &defines[num_defines++];
    strcpy(d->name, "__DATE__");
    d->value = strdup(date_str);
    d->is_func = false;
    d->num_params = 0;
    d->variadic = false;

    /* __TIME__ */
    d = &defines[num_defines++];
    strcpy(d->name, "__TIME__");
    d->value = strdup(time_str);
    d->is_func = false;
    d->num_params = 0;
    d->variadic = false;
}

/* Dynamic predefined macros (expanded at use site) */
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

static Define *find_define(const char *name) {
    for (int i = num_defines - 1; i >= 0; i--) {
        if (strcmp(defines[i].name, name) == 0) return &defines[i];
    }
    return NULL;
}

static void add_define_obj(const char *name, const char *value) {
    /* Remove existing define with same name */
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
        fprintf(stderr, "cpp: too many defines\n");
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

static void add_define_func(const char *name, const char *params_str,
                            const char *body, bool variadic) {
    char params[MAX_PARAMS][64];
    int np = 0;

    if (params_str) {
        const char *p = params_str;
        while (*p && *p != ')') {
            p = skip_ws(p);
            if (*p == ')') break;
            if (*p == '.' && *(p+1) == '.' && *(p+2) == '.') {
                /* variadic marker - skip */
                while (*p && *p != ')') p++;
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

    /* Remove existing */
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
        fprintf(stderr, "cpp: too many defines\n");
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

static void remove_define(const char *name) {
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
        fprintf(stderr, "cpp: too many nested #if\n");
        exit(1);
    }
    CondEntry *e = &cond_stack[cond_depth++];
    e->parent_skipping = (cond_depth > 1) ? is_skipping() : false;
    if (e->parent_skipping) {
        e->state = COND_DONE;  /* doesn't matter, parent is skipping */
    } else {
        e->state = val ? COND_IF : COND_DONE;
    }
}

static void cond_elif(bool val) {
    if (cond_depth == 0) {
        fprintf(stderr, "cpp: #elif without #if\n");
        exit(1);
    }
    CondEntry *e = &cond_stack[cond_depth - 1];
    if (e->parent_skipping) return;  /* parent skipping, ignore */
    if (e->state == COND_IF) {
        e->state = COND_DONE;
    } else if (e->state == COND_DONE) {
        if (val) e->state = COND_ELSE;
        /* else stay DONE */
    } else {
        /* COND_ELSE shouldn't happen here, but handle it */
        e->state = COND_DONE;
    }
}

static void cond_else(void) {
    if (cond_depth == 0) {
        fprintf(stderr, "cpp: #else without #if\n");
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
        fprintf(stderr, "cpp: #endif without #if\n");
        exit(1);
    }
    cond_depth--;
}

static long eval_expr(const char **pp);

static void tok_next(const char **pp, Token *tok) {
    const char *p = skip_ws(*pp);

    if (*p == '\0' || *p == '\n') {
        tok->type = TOK_EOF;
        *pp = p;
        return;
    }

    if (isdigit((unsigned char)*p)) {
        long val = 0;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            while (isxdigit((unsigned char)*p)) {
                int d;
                if (*p >= '0' && *p <= '9') d = *p - '0';
                else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
                else d = *p - 'A' + 10;
                val = val * 16 + d;
                p++;
            }
        } else if (p[0] == '0' && isdigit((unsigned char)p[1])) {
            /* octal */
            p++;
            while (*p >= '0' && *p <= '7') {
                val = val * 8 + (*p - '0');
                p++;
            }
        } else {
            while (isdigit((unsigned char)*p)) {
                val = val * 10 + (*p - '0');
                p++;
            }
        }
        /* Skip optional L/U suffixes */
        while (*p == 'L' || *p == 'l' || *p == 'U' || *p == 'u') p++;
        tok->type = TOK_NUM;
        tok->num = val;
        *pp = p;
        return;
    }

    /* Character constant */
    if (*p == '\'') {
        p++;
        long val = 0;
        if (*p == '\\') {
            p++;
            switch (*p) {
                case 'n': val = '\n'; break;
                case 't': val = '\t'; break;
                case 'r': val = '\r'; break;
                case '0': val = '\0'; break;
                case '\\': val = '\\'; break;
                case '\'': val = '\''; break;
                case '"': val = '"'; break;
                case 'a': val = '\a'; break;
                case 'b': val = '\b'; break;
                case 'f': val = '\f'; break;
                case 'v': val = '\v'; break;
                default: val = *p; break;
            }
            p++;
        } else if (*p != '\'') {
            val = (unsigned char)*p;
            p++;
        }
        if (*p == '\'') p++;
        tok->type = TOK_NUM;
        tok->num = val;
        *pp = p;
        return;
    }

    /* Identifier */
    if (is_id_start(*p)) {
        int i = 0;
        while (is_id_cont(*p) && i < 255) {
            tok->ident[i++] = *p++;
        }
        tok->ident[i] = '\0';
        tok->type = TOK_IDENT;
        *pp = p;
        return;
    }

    /* Two-character operators */
    if ((p[0] == '&' && p[1] == '&') || (p[0] == '|' && p[1] == '|') ||
        (p[0] == '=' && p[1] == '=') || (p[0] == '!' && p[1] == '=') ||
        (p[0] == '<' && p[1] == '=') || (p[0] == '>' && p[1] == '=') ||
        (p[0] == '<' && p[1] == '<') || (p[0] == '>' && p[1] == '>')) {
        tok->type = TOK_OP;
        tok->op[0] = p[0];
        tok->op[1] = p[1];
        tok->op[2] = '\0';
        *pp = p + 2;
        return;
    }

    /* Single character operators and punctuation */
    if (*p == '(') { tok->type = TOK_LPAREN; *pp = p + 1; return; }
    if (*p == ')') { tok->type = TOK_RPAREN; *pp = p + 1; return; }
    if (*p == '?') { tok->type = TOK_QUESTION; *pp = p + 1; return; }
    if (*p == ':') { tok->type = TOK_COLON; *pp = p + 1; return; }

    if (strchr("+-*/%&|^~!<>", *p)) {
        tok->type = TOK_OP;
        tok->op[0] = *p;
        tok->op[1] = '\0';
        *pp = p + 1;
        return;
    }

    /* Skip unknown */
    p++;
    *pp = p;
    tok->type = TOK_EOF;
}

static long eval_primary(const char **pp) {
    Token tok;
    tok_next(pp, &tok);

    if (tok.type == TOK_NUM) return tok.num;

    if (tok.type == TOK_IDENT) {
        if (strcmp(tok.ident, "defined") == 0) {
            tok_next(pp, &tok);
            bool has_paren = (tok.type == TOK_LPAREN);
            if (has_paren) tok_next(pp, &tok);
            /* tok should be IDENT */
            long val = find_define(tok.ident) ? 1 : 0;
            if (has_paren) tok_next(pp, &tok);  /* eat ')' */
            return val;
        }
        Define *d = find_define(tok.ident);
        if (d && !d->is_func) {
            const char *vp = d->value;
            Token vtok;
            tok_next(&vp, &vtok);
            if (vtok.type == TOK_NUM) return vtok.num;
        }
        return 0;
    }

    if (tok.type == TOK_LPAREN) {
        long val = eval_expr(pp);
        tok_next(pp, &tok);  /* eat ')' */
        return val;
    }

    return 0;
}

static long eval_unary(const char **pp) {
    const char *save = *pp;
    Token tok;
    tok_next(pp, &tok);

    if (tok.type == TOK_OP) {
        if (tok.op[0] == '!' && tok.op[1] == '\0') return !eval_unary(pp);
        if (tok.op[0] == '~' && tok.op[1] == '\0') return ~eval_unary(pp);
        if (tok.op[0] == '-' && tok.op[1] == '\0') return -eval_unary(pp);
        if (tok.op[0] == '+' && tok.op[1] == '\0') return eval_unary(pp);
    }
    *pp = save;
    return eval_primary(pp);
}

static long eval_mul(const char **pp) {
    long left = eval_unary(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP &&
            ((tok.op[0] == '*' && tok.op[1] == '\0') ||
             (tok.op[0] == '/' && tok.op[1] == '\0') ||
             (tok.op[0] == '%' && tok.op[1] == '\0'))) {
            long right = eval_unary(pp);
            if (tok.op[0] == '*') left *= right;
            else if (tok.op[0] == '/') left = right ? left / right : 0;
            else left = right ? left % right : 0;
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_add(const char **pp) {
    long left = eval_mul(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP &&
            ((tok.op[0] == '+' && tok.op[1] == '\0') ||
             (tok.op[0] == '-' && tok.op[1] == '\0'))) {
            long right = eval_mul(pp);
            if (tok.op[0] == '+') left += right;
            else left -= right;
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_shift(const char **pp) {
    long left = eval_add(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP &&
            ((tok.op[0] == '<' && tok.op[1] == '<') ||
             (tok.op[0] == '>' && tok.op[1] == '>'))) {
            long right = eval_add(pp);
            if (tok.op[0] == '<') left <<= right;
            else left >>= right;
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_rel(const char **pp) {
    long left = eval_shift(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP) {
            long right = eval_shift(pp);
            if (tok.op[0] == '<' && tok.op[1] == '\0') left = left < right;
            else if (tok.op[0] == '>' && tok.op[1] == '\0') left = left > right;
            else if (tok.op[0] == '<' && tok.op[1] == '=') left = left <= right;
            else if (tok.op[0] == '>' && tok.op[1] == '=') left = left >= right;
            else { *pp = save; break; }
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_eq(const char **pp) {
    long left = eval_rel(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP &&
            ((tok.op[0] == '=' && tok.op[1] == '=') ||
             (tok.op[0] == '!' && tok.op[1] == '='))) {
            long right = eval_rel(pp);
            if (tok.op[0] == '=') left = (left == right);
            else left = (left != right);
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_bitand(const char **pp) {
    long left = eval_eq(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP && tok.op[0] == '&' && tok.op[1] == '\0') {
            left &= eval_eq(pp);
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_bitxor(const char **pp) {
    long left = eval_bitand(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP && tok.op[0] == '^' && tok.op[1] == '\0') {
            left ^= eval_bitand(pp);
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_bitor(const char **pp) {
    long left = eval_bitxor(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP && tok.op[0] == '|' && tok.op[1] == '\0') {
            left |= eval_bitxor(pp);
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_logand(const char **pp) {
    long left = eval_bitor(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP && tok.op[0] == '&' && tok.op[1] == '&') {
            long right = eval_bitor(pp);
            left = left && right;
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_logor(const char **pp) {
    long left = eval_logand(pp);
    while (1) {
        Token tok;
        const char *save = *pp;
        tok_next(pp, &tok);
        if (tok.type == TOK_OP && tok.op[0] == '|' && tok.op[1] == '|') {
            long right = eval_logand(pp);
            left = left || right;
        } else {
            *pp = save;
            break;
        }
    }
    return left;
}

static long eval_ternary(const char **pp) {
    long cond = eval_logor(pp);
    Token tok;
    const char *save = *pp;
    tok_next(pp, &tok);
    if (tok.type == TOK_QUESTION) {
        long true_val = eval_expr(pp);
        tok_next(pp, &tok);  /* eat ':' */
        long false_val = eval_ternary(pp);
        return cond ? true_val : false_val;
    }
    *pp = save;
    return cond;
}

static long eval_expr(const char **pp) {
    return eval_ternary(pp);
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
    bool in_char = false;

    for (size_t i = 0; src[i]; i++) {
        char c = src[i];
        char next = src[i + 1];

        if (in_block) {
            if (c == '*' && next == '/') {
                in_block = false;
                buf_appendc(&b, ' ');
                i++;
            }
            if (c == '\n') buf_appendc(&b, '\n');
            continue;
        }

        if (in_string) {
            buf_appendc(&b, c);
            if (c == '\\' && next) {
                buf_appendc(&b, next);
                i++;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (in_char) {
            buf_appendc(&b, c);
            if (c == '\\' && next) {
                buf_appendc(&b, next);
                i++;
            } else if (c == '\'') {
                in_char = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
            buf_appendc(&b, c);
            continue;
        }
        if (c == '\'') {
            in_char = true;
            buf_appendc(&b, c);
            continue;
        }
        if (c == '/' && next == '*') {
            in_block = true;
            i++;
            continue;
        }
        if (c == '/' && next == '/') {
            while (src[i] && src[i] != '\n') i++;
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
            i++;  /* skip both */
            continue;
        }
        buf_appendc(&b, src[i]);
    }

    return buf_steal(&b);
}

static bool is_whole_ident(const char *str, const char *p, const char *name) {
    size_t len = strlen(name);
    if (strncmp(p, name, len) != 0) return false;
    if (len > 0 && is_id_cont(p[len])) return false;
    if (p > str && is_id_cont(p[-1])) return false;
    return true;
}

static char *stringify(const char *arg) {
    Buf b;
    buf_init(&b);
    buf_appendc(&b, '"');

    const char *p = skip_ws(arg);
    const char *end = arg + strlen(arg);
    while (end > p && isspace((unsigned char)end[-1])) end--;

    while (p < end) {
        if (*p == '"' || *p == '\\') {
            buf_appendc(&b, '\\');
            buf_appendc(&b, *p);
        } else {
            buf_appendc(&b, *p);
        }
        p++;
    }

    buf_appendc(&b, '"');
    return buf_steal(&b);
}

static char *token_paste(const char *left, const char *right) {
    Buf b;
    buf_init(&b);
    const char *p = left;
    while (*p && isspace((unsigned char)*p)) p++;
    while (*p && !isspace((unsigned char)*p)) buf_appendc(&b, *p++);
    p = right;
    while (*p && isspace((unsigned char)*p)) p++;
    while (*p && !isspace((unsigned char)*p)) buf_appendc(&b, *p++);
    return buf_steal(&b);
}

static char *expand_macros(const char *input, int depth);

static char **parse_macro_args(const char **pp, int *count, bool *had_va_args) {
    *count = 0;
    *had_va_args = false;

    char **args = malloc(sizeof(char*) * (MAX_PARAMS + 1));
    Buf cur;
    buf_init(&cur);
    int paren = 0;

    const char *p = *pp;
    while (*p) {
        if (*p == '(' ) { paren++; buf_appendc(&cur, *p); p++; continue; }
        if (*p == ')') {
            if (paren == 0) {
                p++;
                break;
            }
            paren--;
            buf_appendc(&cur, *p);
            p++;
            continue;
        }
        if (*p == ',' && paren == 0) {
            args[(*count)++] = buf_steal(&cur);
            buf_init(&cur);
            p++;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            char q = *p;
            buf_appendc(&cur, *p); p++;
            while (*p && *p != q) {
                if (*p == '\\' && *(p+1)) {
                    buf_appendc(&cur, *p);
                    buf_appendc(&cur, *(p+1));
                    p += 2;
                } else {
                    buf_appendc(&cur, *p);
                    p++;
                }
            }
            if (*p == q) { buf_appendc(&cur, *p); p++; }
            continue;
        }
        buf_appendc(&cur, *p);
        p++;
    }

    if (cur.len > 0 || *count > 0) {
        args[(*count)++] = buf_steal(&cur);
    } else {
        buf_free(&cur);
    }

    *pp = p;
    return args;
}

static char *substitute_body(const Define *d, char **args, int arg_count,
                             int depth) {
    Buf result;
    buf_init(&result);

    const char *p = d->value;

    while (*p) {
        /* Check for ## (token paste) */
        if (p[0] == '#' && p[1] == '#') {
            p += 2;
            p = skip_ws(p);

            Buf right;
            buf_init(&right);

            if (*p == '#' && *(p+1) != '#') {
                /* # stringify */
                p++;
                p = skip_ws(p);
                int i = 0;
                char name[64];
                while (is_id_cont(*p) && i < 63) name[i++] = *p++;
                name[i] = '\0';
                for (int j = 0; j < d->num_params; j++) {
                    if (strcmp(d->params[j], name) == 0 && j < arg_count) {
                        char *s = stringify(args[j]);
                        buf_append(&right, s);
                        free(s);
                        break;
                    }
                }
            } else if (is_id_start(*p)) {
                int i = 0;
                char name[64];
                while (is_id_cont(*p) && i < 63) name[i++] = *p++;
                name[i] = '\0';

                bool found = false;
                for (int j = 0; j < d->num_params; j++) {
                    if (strcmp(d->params[j], name) == 0 && j < arg_count) {
                        buf_append(&right, args[j]);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    buf_append(&right, name);
                }
            }

            while (result.len > 0 && isspace((unsigned char)result.data[result.len-1])) {
                result.data[--result.len] = '\0';
            }

            /* Paste */
            char *pasted = token_paste(result.data, right.data);
            buf_free(&result);
            buf_init(&result);
            buf_append(&result, pasted);
            free(pasted);
            buf_free(&right);
            continue;
        }

        /* Check for # (stringify) - but not ## */
        if (*p == '#' && *(p+1) != '#') {
            const char *save = p;
            p++;
            p = skip_ws(p);
            if (is_id_start(*p)) {
                int i = 0;
                char name[64];
                while (is_id_cont(*p) && i < 63) name[i++] = *p++;
                name[i] = '\0';

                for (int j = 0; j < d->num_params; j++) {
                    if (strcmp(d->params[j], name) == 0 && j < arg_count) {
                        char *s = stringify(args[j]);
                        buf_append(&result, s);
                        free(s);
                        goto next;
                    }
                }
            }
            /* Not a valid stringify, restore */
            p = save;
            buf_appendc(&result, *p);
            p++;
            continue;
        }

        if (is_id_start(*p)) {
            int i = 0;
            char name[256];
            while (is_id_cont(*p) && i < 255) name[i++] = *p++;
            name[i] = '\0';

            /* Check for __VA_ARGS__ */
            if (strcmp(name, "__VA_ARGS__") == 0 && d->variadic) {
                /* Collect all args beyond num_params */
                Buf va;
                buf_init(&va);
                for (int j = d->num_params; j < arg_count; j++) {
                    if (j > d->num_params) buf_appendc(&va, ',');
                    buf_append(&va, args[j]);
                }
                char *expanded = expand_macros(va.data, depth + 1);
                buf_append(&result, expanded);
                free(expanded);
                buf_free(&va);
                continue;
            }

            bool found = false;
            for (int j = 0; j < d->num_params; j++) {
                if (strcmp(d->params[j], name) == 0 && j < arg_count) {
                    char *expanded = expand_macros(args[j], depth + 1);
                    buf_append(&result, expanded);
                    free(expanded);
                    found = true;
                    break;
                }
            }
            if (!found) {
                buf_append(&result, name);
            }
            continue;
        }

        /* String/char literal - pass through */
        if (*p == '"' || *p == '\'') {
            char q = *p;
            buf_appendc(&result, *p); p++;
            while (*p && *p != q) {
                if (*p == '\\' && *(p+1)) {
                    buf_appendc(&result, *p);
                    buf_appendc(&result, *(p+1));
                    p += 2;
                } else {
                    buf_appendc(&result, *p);
                    p++;
                }
            }
            if (*p == q) { buf_appendc(&result, *p); p++; }
            continue;
        }

        buf_appendc(&result, *p);
        p++;

next:
        continue;
    }

    return buf_steal(&result);
}

static char *expand_macros(const char *input, int depth) {
    if (depth > MAX_EXPAND_DEPTH) {
        return strdup(input);
    }

    Buf result;
    buf_init(&result);

    const char *p = input;

    while (*p) {
        /* Skip string/char literals */
        if (*p == '"' || *p == '\'') {
            char q = *p;
            buf_appendc(&result, *p); p++;
            while (*p && *p != q) {
                if (*p == '\\' && *(p+1)) {
                    buf_appendc(&result, *p);
                    buf_appendc(&result, *(p+1));
                    p += 2;
                } else {
                    buf_appendc(&result, *p);
                    p++;
                }
            }
            if (*p == q) { buf_appendc(&result, *p); p++; }
            continue;
        }

        /* Check for identifier */
        if (is_id_start(*p)) {
            const char *start = p;
            int i = 0;
            char name[256];
            while (is_id_cont(*p) && i < 255) name[i++] = *p++;
            name[i] = '\0';

            /* Check dynamic macros first */
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

            if (d->is_func) {
                const char *after = skip_ws(p);
                if (*after != '(') {
                    /* Not a function invocation, don't expand */
                    buf_append(&result, name);
                    continue;
                }
                after++;  /* skip '(' */

                bool had_va;
                int arg_count;
                char **args = parse_macro_args(&after, &arg_count, &had_va);
                p = after;

                /* Check arg count (allow variadic to have more) */
                if (!d->variadic && arg_count != d->num_params) {
                    buf_appendc(&result, '(');
                    for (int j = 0; j < arg_count; j++) {
                        if (j > 0) buf_appendc(&result, ',');
                        buf_append(&result, args[j]);
                    }
                    buf_appendc(&result, ')');
                    for (int j = 0; j < arg_count; j++) free(args[j]);
                    free(args);
                    continue;
                }

                char *substituted = substitute_body(d, args, arg_count, depth);

                char *saved_value = d->value;
                d->value = strdup("");  /* hide it */
                char *reexpanded = expand_macros(substituted, depth + 1);
                d->value = saved_value;

                buf_append(&result, reexpanded);

                free(substituted);
                free(reexpanded);
                for (int j = 0; j < arg_count; j++) free(args[j]);
                free(args);
            } else {
                char *saved_value = d->value;
                d->value = strdup("");
                char *expanded = expand_macros(saved_value, depth + 1);
                d->value = saved_value;

                buf_append(&result, expanded);
                free(expanded);
            }
            continue;
        }

        buf_appendc(&result, *p);
        p++;
    }

    return buf_steal(&result);
}

static const char *get_directive_name(const char *line, char *name, size_t name_sz) {
    const char *p = skip_ws(line);
    if (*p != '#') return NULL;
    p++;
    p = skip_ws(p);

    size_t i = 0;
    while (*p && !isspace((unsigned char)*p) && i < name_sz - 1) {
        name[i++] = *p++;
    }
    name[i] = '\0';
    return p;
}

static const char *get_directive_body(const char *line) {
    char name[64];
    const char *p = get_directive_name(line, name, sizeof(name));
    if (!p) return NULL;
    return skip_ws(p);
}

static bool is_directive_line(const char *line) {
    const char *p = skip_ws(line);
    return *p == '#';
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

typedef struct {
    Buf lines;
    int  *line_nums;   /* original line number for each output line */
    const char **files; /* source file for each output line */
    int  count;
    int  cap;
} Output;

static void out_init(Output *o) {
    buf_init(&o->lines);
    o->cap = 4096;
    o->line_nums = malloc(o->cap * sizeof(int));
    o->files = malloc(o->cap * sizeof(const char*));
    o->count = 0;
}

static void out_free(Output *o) {
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

static void process_file(const char *filepath, Output *output);

static void process_lines(const char *filepath, const char *content, Output *output) {
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

        if (is_directive_line(line)) {
            char dir_name[64];
            const char *body = get_directive_body(line);

            if (!get_directive_name(line, dir_name, sizeof(dir_name))) {
                free(line);
                continue;
            }

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

            if (!is_active()) {
                free(line);
                continue;
            }

            if (strcmp(dir_name, "define") == 0) {
                const char *rest = skip_ws(body);

                char name[256];
                int ni = 0;
                while (*rest && (is_id_cont(*rest)) && ni < 255) {
                    name[ni++] = *rest++;
                }
                name[ni] = '\0';

                if (ni == 0) {
                    free(line);
                    continue;
                }

                if (*rest == '(') {
                    rest++;
                    const char *paren_end = strchr(rest, ')');
                    if (!paren_end) {
                        fprintf(stderr, "cpp: unterminated macro parameter list\n");
                        free(line);
                        continue;
                    }

                    char params_buf[512];
                    size_t plen = paren_end - rest;
                    memcpy(params_buf, rest, plen);
                    params_buf[plen] = '\0';

                    bool variadic = (strstr(params_buf, "...") != NULL);

                    rest = paren_end + 1;
                    rest = skip_ws(rest);

                    char *macro_body = strdup(rest);
                    size_t blen = strlen(macro_body);
                    while (blen > 0 && (macro_body[blen-1] == '\n' ||
                           isspace((unsigned char)macro_body[blen-1]))) {
                        macro_body[--blen] = '\0';
                    }

                    add_define_func(name, params_buf, macro_body, variadic);
                    free(macro_body);
                } else {
                    rest = skip_ws(rest);
                    char *value = strdup(rest);
                    size_t vlen = strlen(value);
                    while (vlen > 0 && (value[vlen-1] == '\n' ||
                           isspace((unsigned char)value[vlen-1]))) {
                        value[--vlen] = '\0';
                    }
                    add_define_obj(name, value);
                    free(value);
                }
                free(line);
                continue;
            }

            if (strcmp(dir_name, "undef") == 0) {
                char name[256];
                if (sscanf(body, "%255s", name) == 1) {
                    remove_define(name);
                }
                free(line);
                continue;
            }

            if (strcmp(dir_name, "include") == 0) {
                const char *inc = skip_ws(body);
                char fname[512];
                bool quoted = false;

                if (*inc == '"') {
                    quoted = true;
                    inc++;
                    int i = 0;
                    while (*inc && *inc != '"' && i < 511) fname[i++] = *inc++;
                    fname[i] = '\0';
                } else if (*inc == '<') {
                    quoted = false;
                    inc++;
                    int i = 0;
                    while (*inc && *inc != '>' && i < 511) fname[i++] = *inc++;
                    fname[i] = '\0';
                } else {
                    fprintf(stderr, "cpp: unsupported #include syntax at %s:%d\n",
                            filepath, line_num);
                    free(line);
                    continue;
                }

                char *resolved = resolve_include(fname, quoted, dir);
                if (!resolved) {
                    fprintf(stderr, "cpp: cannot find include file '%s' at %s:%d\n",
                            fname, filepath, line_num);
                    free(line);
                    continue;
                }

                if (is_once_file(resolved)) {
                    free(resolved);
                    free(line);
                    continue;
                }

                process_file(resolved, output);
                free(resolved);
                free(line);
                continue;
            }

            if (strcmp(dir_name, "pragma") == 0) {
                const char *pragma_body = skip_ws(body);
                if (strncmp(pragma_body, "once", 4) == 0 &&
                    (pragma_body[4] == '\0' || isspace((unsigned char)pragma_body[4]))) {
                    add_once_file(filepath);
                }
                /* Other pragmas ignored */
                free(line);
                continue;
            }

            if (strcmp(dir_name, "error") == 0) {
                fprintf(stderr, "#error %s (at %s:%d)\n", body, filepath, line_num);
                exit(1);
            }

            if (strcmp(dir_name, "warning") == 0) {
                fprintf(stderr, "#warning %s (at %s:%d)\n", body, filepath, line_num);
                free(line);
                continue;
            }

            if (strcmp(dir_name, "line") == 0) {
                /* #line NUMBER ["FILENAME"] */
                int new_line;
                char new_file[512];
                new_file[0] = '\0';
                if (sscanf(body, "%d %511[^\n]", &new_line, new_file) >= 1) {
                    if (new_file[0]) {
                        /* Strip quotes if present */
                        char *nf = new_file;
                        if (*nf == '"') {
                            nf++;
                            char *end = strchr(nf, '"');
                            if (end) *end = '\0';
                        }
                        current_file = strdup(nf);
                    }
                    current_line = new_line - 1;  /* will be incremented */
                }
                free(line);
                continue;
            }

            /* Unknown directive - warn and skip */
            fprintf(stderr, "cpp: unknown directive #%s at %s:%d\n",
                    dir_name, filepath, line_num);
            free(line);
            continue;
        }

        /* Regular line - skip if not active */
        if (!is_active()) {
            free(line);
            continue;
        }

        /* Expand macros and emit */
        char *expanded = expand_macros(line, 0);
        out_emit(output, expanded, line_num, current_file);
        free(expanded);
        free(line);
    }

    free(joined);
    free(dir);
    current_file = saved_file;
}

static void process_file(const char *filepath, Output *output) {
    char *content = read_file(filepath);
    if (!content) {
        fprintf(stderr, "cpp: cannot open file: %s\n", filepath);
        return;
    }

    /* Emit line marker */
    char marker[2048];
    snprintf(marker, sizeof(marker), "#line 1 \"%s\"\n", filepath);
    out_emit(output, marker, 0, filepath);

    process_lines(filepath, content, output);
    free(content);
}

static void handle_cmdline_define(const char *arg) {
    char name[256];
    const char *p = arg;
    int i = 0;
    while (*p && *p != '=' && is_id_cont(*p) && i < 255) {
        name[i++] = *p++;
    }
    name[i] = '\0';

    if (*p == '=') {
        p++;
        add_define_obj(name, p);
    } else {
        add_define_obj(name, "1");
    }
}

#ifdef PREP_STANDALONE

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <source.c>\n"
        "Options:\n"
        "  -o <output>      Output file (default: stdout)\n"
        "  -I <path>        Add include search path\n"
        "  -D <name>[=val]  Define macro\n"
        "  -U <name>        Undefine macro\n"
        "  -E               Stop after preprocessing (default)\n"
        "  -P               Don't emit #line markers\n"
        "  -trigraphs       Enable trigraphs (not implemented)\n"
        "  --help           Show this help\n",
        prog);
}

int main(int argc, char *argv[]) {
    const char *src_path = NULL;
    const char *out_path = NULL;
    bool emit_line_markers = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strncmp(argv[i], "-I", 2) == 0) {
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
        } else if (strncmp(argv[i], "-U", 2) == 0) {
            const char *name = argv[i] + 2;
            if (!*name && i + 1 < argc) name = argv[++i];
            remove_define(name);
        } else if (strcmp(argv[i], "-P") == 0) {
            emit_line_markers = false;
        } else if (strcmp(argv[i], "-E") == 0) {
            /* no-op, default behavior */
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            src_path = argv[i];
        } else {
            fprintf(stderr, "cpp: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!src_path) {
        fprintf(stderr, "cpp: no input file\n");
        print_usage(argv[0]);
        return 1;
    }

    init_predefined();

    Output output;
    out_init(&output);

    process_file(src_path, &output);

    /* Write output */
    FILE *outf = out_path ? fopen(out_path, "w") : stdout;
    if (!outf) {
        fprintf(stderr, "cpp: cannot open output file: %s\n", out_path);
        return 1;
    }

    if (emit_line_markers) {
        fprintf(outf, "#line 1 \"%s\"\n", src_path);
    }

    const char *prev_file = NULL;
    int prev_line = 0;
    const char *p = output.lines.data;

    for (int i = 0; i < output.count; i++) {
        /* Detect if this is already a #line marker we emitted */
        if (strncmp(p, "#line ", 6) == 0) {
            if (emit_line_markers) {
                fprintf(outf, "%s", p);
            }
            p += strlen(p);
            continue;
        }

        /* Emit #line if file/line changed significantly */
        if (emit_line_markers && output.files[i] != prev_file) {
            fprintf(outf, "#line %d \"%s\"\n", output.line_nums[i], output.files[i]);
            prev_file = output.files[i];
            prev_line = output.line_nums[i];
        } else if (emit_line_markers &&
                   abs(output.line_nums[i] - (prev_line + 1)) > 5) {
            fprintf(outf, "#line %d \"%s\"\n", output.line_nums[i], output.files[i]);
            prev_line = output.line_nums[i];
        }

        fprintf(outf, "%s", p);
        prev_line += /* count newlines in p */ 1;  /* simplified */
        p += strlen(p);
    }

    if (outf != stdout) fclose(outf);

    out_free(&output);

    /* Clean up defines */
    for (int i = 0; i < num_defines; i++) {
        free(defines[i].value);
    }
    for (int i = 0; i < num_once_files; i++) {
        free(once_files[i]);
    }

    return 0;
}

#endif
