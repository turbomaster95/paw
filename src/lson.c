#include "lson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(LsonTranslator *t, size_t line, const char *msg) {
    nu_snprintf(t->last_error, sizeof(t->last_error),
                "LSON Parse Error (Line %zu): %s", line, msg);
}

static char* parse_lson_string(LsonTranslator *t, nu_lexer_t *lexer) {
    nu_buf_t buf;
    nu_buf_init(&buf, t->mm, 32);
    
    // Skip first quote
    nu_lexer_advance_char(lexer);

    while (1) {
        char c = nu_lexer_peek_char(lexer);
        if (c == '\0') {
            set_error(t, lexer->line, "Unterminated string");
            nu_buf_free(&buf);
            return NULL;
        }

        if (c == '\\') {
            nu_lexer_advance_char(lexer);
            char escaped = nu_lexer_advance_char(lexer);
            switch (escaped) {
                case '"':  nu_buf_append_val(&buf, '"');  break;
                case '\\': nu_buf_append_val(&buf, '\\'); break;
                case '/':  nu_buf_append_val(&buf, '/');  break;
                case 'b':  nu_buf_append_val(&buf, '\b'); break;
                case 'f':  nu_buf_append_val(&buf, '\f'); break;
                case 'n':  nu_buf_append_val(&buf, '\n'); break;
                case 'r':  nu_buf_append_val(&buf, '\r'); break;
                case 't':  nu_buf_append_val(&buf, '\t'); break;
                default:   nu_buf_append_val(&buf, escaped); break;
            }
        } else if (c == '"') {
            nu_lexer_advance_char(lexer); // Skip ending quote
            nu_buf_append_val(&buf, '\0');
            
            char *result = (char*)nu_alloc(t->mm, buf.size);
            if (result) memcpy(result, buf.data, buf.size);
            
            nu_buf_free(&buf);
            return result;
        } else {
            nu_buf_append_val(&buf, c);
            nu_lexer_advance_char(lexer);
        }
    }
}

void lson_init(LsonTranslator *t, nu_mm_t *mm) {
    t->mm = mm;
    t->dictionary = nu_map_create(mm, 32);
    t->last_error[0] = '\0';
}

void lson_free(LsonTranslator *t) {
    if (t->dictionary) {
        nu_map_destroy(t->dictionary);
        t->dictionary = NULL;
    }
}

bool lson_load_string(LsonTranslator *t, const char *lson_text) {
    if (!lson_text) return false;

    nu_lexer_t lexer;
    nu_lexer_init(&lexer, lson_text);

    // skip utf-8 bom (if there)
    const uint8_t *bytes = (const uint8_t*)lson_text;
    if (bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        lexer.cursor = 3;
    }

    nu_lexer_skip_whitespace(&lexer);

    if (nu_lexer_peek_char(&lexer) != '{') {
        set_error(t, lexer.line, "Expected '{'");
        return false;
    }
    nu_lexer_advance_char(&lexer);

    while (nu_lexer_peek_char(&lexer) != '\0') {
        nu_lexer_skip_whitespace(&lexer);

        char c = nu_lexer_peek_char(&lexer);
        if (c == '}') {
            return true;
        }

        if (c != '"') {
            set_error(t, lexer.line, "Expected '\"' for key");
            return false;
        }

        char *key = parse_lson_string(t, &lexer);
        if (!key) return false;

        nu_lexer_skip_whitespace(&lexer);
        if (nu_lexer_peek_char(&lexer) != ':') {
            set_error(t, lexer.line, "Expected ':'");
            return false;
        }
        nu_lexer_advance_char(&lexer);

        nu_lexer_skip_whitespace(&lexer);
        if (nu_lexer_peek_char(&lexer) != '"') {
            set_error(t, lexer.line, "Expected '\"' for value");
            return false;
        }

        char *value = parse_lson_string(t, &lexer);
        if (!value) return false;

        nu_map_set(t->dictionary, key, value);

        nu_lexer_skip_whitespace(&lexer);
        c = nu_lexer_peek_char(&lexer);
        if (c == ',') {
            nu_lexer_advance_char(&lexer);
            nu_lexer_skip_whitespace(&lexer);
            if (nu_lexer_peek_char(&lexer) == '}') {
                return true;
            }
        } else if (c == '}') {
            return true;
        } else {
            set_error(t, lexer.line, "Expected ',' or '}'");
            return false;
        }
    }

    set_error(t, lexer.line, "Missing '}'");
    return false;
}

bool lson_load_file(LsonTranslator *t, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        nu_snprintf(t->last_error, sizeof(t->last_error), "Failed to open file: %s", filepath);
        return false;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        fclose(file);
        return false;
    }

    char *buffer = (char*)nu_alloc(t->mm, size + 1);
    if (!buffer) {
        fclose(file);
        nu_snprintf(t->last_error, sizeof(t->last_error), "Memory allocation failed");
        return false;
    }

    size_t read_bytes = fread(buffer, 1, size, file);
    fclose(file);
    buffer[read_bytes] = '\0';

    bool success = lson_load_string(t, buffer);
    nu_free(t->mm, buffer);
    return success;
}

const char* lson_tr(const LsonTranslator *t, const char *key) {
    if (!t->dictionary || !key) return key;
    void *val = nu_map_get(t->dictionary, key);
    return val ? (const char*)val : key;
}

const char* lson_get_last_error(const LsonTranslator *t) {
    return t->last_error;
}
