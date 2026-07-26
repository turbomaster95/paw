#ifndef GLOG_H
#define GLOG_H

#include <stdio.h>
#include <stdarg.h>

typedef enum {
    GLOG_INFO,
    GLOG_NOTE,
    GLOG_WARNING,
    GLOG_ERROR,
    GLOG_FATAL
} glog_level_t;

typedef struct {
    int use_color;      /* -1 = auto, 0 = never, 1 = always */
    int show_source;    /* print the source line + caret    */
    int show_column;    /* include column in location       */
    FILE *out;          /* default = stderr                 */
    const char *prefix; /* e.g. "dcc"                       */
} glog_config_t;

extern glog_config_t glog_config;

void glog_init(void);
void glog_configure(int use_color, int show_source, int show_column, FILE *out);

void glog_log(const char *file, int line, int col, glog_level_t level, const char *fmt, ...);
void glog_vlog(const char *file, int line, int col, glog_level_t level, const char *fmt, va_list ap);

#define GLOG_INFO(...)   glog_log(__FILE__, __LINE__, 0, GLOG_INFO,    __VA_ARGS__)
#define GLOG_NOTE(...)   glog_log(__FILE__, __LINE__, 0, GLOG_NOTE,    __VA_ARGS__)
#define GLOG_WARN(...)   glog_log(__FILE__, __LINE__, 0, GLOG_WARNING, __VA_ARGS__)
#define GLOG_ERROR(...)  glog_log(__FILE__, __LINE__, 0, GLOG_ERROR,   __VA_ARGS__)
#define GLOG_FATAL(...)  glog_log(__FILE__, __LINE__, 0, GLOG_FATAL,   __VA_ARGS__)

/* Explicit column variants */
#define GLOG_INFO_C(c,...)   glog_log(__FILE__, __LINE__, (c), GLOG_INFO,    __VA_ARGS__)
#define GLOG_NOTE_C(c,...)   glog_log(__FILE__, __LINE__, (c), GLOG_NOTE,    __VA_ARGS__)
#define GLOG_WARN_C(c,...)   glog_log(__FILE__, __LINE__, (c), GLOG_WARNING, __VA_ARGS__)
#define GLOG_ERROR_C(c,...)  glog_log(__FILE__, __LINE__, (c), GLOG_ERROR,   __VA_ARGS__)
#define GLOG_FATAL_C(c,...)  glog_log(__FILE__, __LINE__, (c), GLOG_FATAL,   __VA_ARGS__)

#ifdef GLOG_IMPL

#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#define IS_TTY(fd) _isatty(_fileno(fd))
#else
#include <unistd.h>
#define IS_TTY(fd) isatty(fileno(fd))
#endif

glog_config_t glog_config = { -1, 1, 1, NULL, NULL };

void glog_init(void) {
    if (!glog_config.out) glog_config.out = stderr;
}

void glog_configure(int use_color, int show_source, int show_column, FILE *out) {
    glog_config.use_color = use_color;
    glog_config.show_source = show_source;
    glog_config.show_column = show_column;
    glog_config.out = out ? out : stderr;
}

static const char *level_str(glog_level_t lvl) {
    switch (lvl) {
        case GLOG_INFO:    return "info";
        case GLOG_NOTE:    return "note";
        case GLOG_WARNING: return "warning";
        case GLOG_ERROR:   return "error";
        case GLOG_FATAL:   return "fatal error";
    }
    return "unknown";
}

static const char *level_color(glog_level_t lvl) {
    switch (lvl) {
        case GLOG_INFO:    return "\033[1;32m"; /* bold green  */
        case GLOG_NOTE:    return "\033[1;36m"; /* bold cyan   */
        case GLOG_WARNING: return "\033[1;35m"; /* bold magenta*/
        case GLOG_ERROR:   return "\033[1;31m"; /* bold red    */
        case GLOG_FATAL:   return "\033[1;31m"; /* bold red    */
    }
    return "\033[0m";
}

static int visual_column(const char *s, int col) {
    int v = 0;
    for (int i = 0; i < col - 1 && s[i]; i++) {
        if (s[i] == '\t') v += 8 - ((v - 1) % 8);
        else v++;
    }
    return v;
}

static void print_context(const char *file, int line, int col) {
    if (!glog_config.show_source || !file || line <= 0) return;
    FILE *f = fopen(file, "r");
    if (!f) return;

    char buf[1024];
    int n = 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (++n == line) {
            size_t len = strlen(buf);
            if (len && buf[len-1] == '\n') buf[len-1] = '\0';

            fprintf(glog_config.out, "%5d | %s\n", line, buf);

            if (col > 0) {
                int v = visual_column(buf, col);
                fprintf(glog_config.out, "      | ");
		for (int i = 0; i < v - 1; i++) fputc(' ', glog_config.out);
                fprintf(glog_config.out, "^\n");
            }
            break;
        }
        if (n > line + 2) break; /* safety */
    }
    fclose(f);
}

void glog_vlog(const char *file, int line, int col,
               glog_level_t lvl, const char *fmt, va_list ap) {
    if (!glog_config.out) glog_config.out = stderr;

    int color = glog_config.use_color;
    if (color < 0) color = IS_TTY(glog_config.out);

    if (glog_config.prefix)
        fprintf(glog_config.out, "%s: ", glog_config.prefix);

    if (file) {
        fprintf(glog_config.out, "%s:", file);
        if (line > 0) fprintf(glog_config.out, "%d:", line);
        if (glog_config.show_column && col > 0) fprintf(glog_config.out, "%d:", col);
        fputc(' ', glog_config.out);
    }

    if (color)
        fprintf(glog_config.out, "%s%s:\033[0m ", level_color(lvl), level_str(lvl));
    else
        fprintf(glog_config.out, "%s: ", level_str(lvl));

    vfprintf(glog_config.out, fmt, ap);
    fputc('\n', glog_config.out);

    print_context(file, line, col);

    if (lvl == GLOG_FATAL) abort();
}

void glog_log(const char *file, int line, int col,
              glog_level_t lvl, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    glog_vlog(file, line, col, lvl, fmt, ap);
    va_end(ap);
}

#endif /* GLOG_IMPL */

#endif /* GLOG_H */
