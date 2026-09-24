#include "pawffi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PAW_SDL_USE_SDL3
#  define PAW_SDL3 1
#  include <SDL3/SDL.h>
#else
#  define PAW_SDL3 0
#  include <SDL2/SDL.h>
#endif

#if !PAW_SDL3
#  define PAW_WINDOW_SHOWN SDL_WINDOW_SHOWN      /* removed in SDL3 */
#else
#  define PAW_WINDOW_SHOWN 0
#endif

static int paw_init(Uint32 flags) {
#if PAW_SDL3
    return SDL_Init(flags) ? 0 : -1;             /* SDL3 returns bool */
#else
    return (SDL_Init(flags) == 0) ? 0 : -1;
#endif
}

static SDL_Window *paw_create_window(const char *title, int w, int h, Uint32 flags) {
#if PAW_SDL3
    return SDL_CreateWindow(title, w, h, flags); /* position is OS-centered */
#else
    return SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            w, h, flags);
#endif
}

static SDL_Renderer *paw_create_renderer(SDL_Window *win, int vsync) {
#if PAW_SDL3
    SDL_Renderer *r = SDL_CreateRenderer(win, NULL);        /* best available */
    if (!r) r = SDL_CreateRenderer(win, "software");        /* fallback */
    if (r && vsync) (void)SDL_SetRenderVSync(r, 1);
    return r;
#else
    SDL_Renderer *r = SDL_CreateRenderer(win, -1,
            SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!r)                                                 /* sw fallback */
        r = SDL_CreateRenderer(win, -1, (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    return r;
#endif
}

static int paw_clear(SDL_Renderer *r) {
#if PAW_SDL3
    return SDL_RenderClear(r) ? 0 : -1;
#else
    return SDL_RenderClear(r);
#endif
}

static void paw_present(SDL_Renderer *r) {
    SDL_RenderPresent(r);                        /* void in SDL2, bool in SDL3 */
}

static void paw_set_draw_color(SDL_Renderer *r, unsigned char cr, unsigned char cg,
                               unsigned char cb, unsigned char ca) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
}

static int paw_draw_rect(SDL_Renderer *r, int x, int y, int w, int h) {
#if PAW_SDL3
    SDL_FRect rc = { (float)x, (float)y, (float)w, (float)h };
    return SDL_RenderRect(r, &rc) ? 0 : -1;
#else
    SDL_Rect rc = { x, y, w, h };
    return SDL_RenderDrawRect(r, &rc);
#endif
}

static int paw_fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
#if PAW_SDL3
    SDL_FRect rc = { (float)x, (float)y, (float)w, (float)h };
    return SDL_RenderFillRect(r, &rc) ? 0 : -1;
#else
    SDL_Rect rc = { x, y, w, h };
    return SDL_RenderFillRect(r, &rc);
#endif
}

static int paw_draw_line(SDL_Renderer *r, int x1, int y1, int x2, int y2) {
#if PAW_SDL3
    return SDL_RenderLine(r, (float)x1, (float)y1, (float)x2, (float)y2) ? 0 : -1;
#else
    return SDL_RenderDrawLine(r, x1, y1, x2, y2);
#endif
}

static int paw_render_texture(SDL_Renderer *r, SDL_Texture *t, int x, int y, int w, int h) {
#if PAW_SDL3
    SDL_FRect dst = { (float)x, (float)y, (float)w, (float)h };
    return SDL_RenderTexture(r, t, NULL, &dst) ? 0 : -1;
#else
    SDL_Rect dst = { x, y, w, h };
    return SDL_RenderCopy(r, t, NULL, &dst);
#endif
}

static int paw_texture_size(SDL_Texture *t, int *w, int *h) {
#if PAW_SDL3
#  if SDL_VERSION_ATLEAST(3, 2, 0)
    float fw = 0.0f, fh = 0.0f;
    if (!SDL_GetTextureSize(t, &fw, &fh)) return -1;
    *w = (int)fw; *h = (int)fh;
    return 0;
#  else
    (void)t; (void)w; (void)h;
    return -1;
#  endif
#else
    return SDL_QueryTexture(t, NULL, NULL, w, h);
#endif
}

static void paw_free_surface(SDL_Surface *s) {
#if PAW_SDL3
    SDL_DestroySurface(s);                       /* renamed in SDL3 */
#else
    SDL_FreeSurface(s);
#endif
}

static int renderer_has_vsync(SDL_Renderer *r) {
#if PAW_SDL3
    int vs = 0;
    return (SDL_GetRenderVSync(r, &vs) && vs != 0) ? 1 : 0;
#else
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(r, &info) == 0)
        return (info.flags & SDL_RENDERER_PRESENTVSYNC) ? 1 : 0;
    return 0;
#endif
}

static void paw_query_mouse(int *x, int *y) {
#if PAW_SDL3
    float fx = 0.0f, fy = 0.0f;
    (void)SDL_GetMouseState(&fx, &fy);
    *x = (int)fx; *y = (int)fy;
#else
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    *x = mx; *y = my;
#endif
}

static int paw_mouse_down(int button) {
    if (button < 1 || button > 32) return 0;
#if PAW_SDL3
    SDL_MouseButtonFlags st = SDL_GetMouseState(NULL, NULL);
    return (st & SDL_BUTTON_MASK(button)) ? 1 : 0;
#else
    Uint32 st = SDL_GetMouseState(NULL, NULL);
    return (st & SDL_BUTTON(button)) ? 1 : 0;
#endif
}

enum {
    PAW_EVT_NONE             = 0,
    PAW_EVT_QUIT             = 1,
    PAW_EVT_KEY_DOWN         = 2,
    PAW_EVT_KEY_UP           = 3,
    PAW_EVT_WINDOW_RESIZED   = 4,
    PAW_EVT_MOUSE_MOTION     = 5,
    PAW_EVT_MOUSE_DOWN       = 6,
    PAW_EVT_MOUSE_UP         = 7
};

#if PAW_SDL3
#  define PAW_EV_QUIT     SDL_EVENT_QUIT
#  define PAW_EV_KEY_DOWN SDL_EVENT_KEY_DOWN
#  define PAW_EV_KEY_UP   SDL_EVENT_KEY_UP
#  define PAW_KEY_OF(ev)  ((ev)->key.key)
#else
#  define PAW_EV_QUIT     SDL_QUIT
#  define PAW_EV_KEY_DOWN SDL_KEYDOWN
#  define PAW_EV_KEY_UP   SDL_KEYUP
#  define PAW_KEY_OF(ev)  ((ev)->key.keysym.sym)
#endif

static int g_last_key = 0;
static int g_mouse_x = 0, g_mouse_y = 0;

static int module_poll_event(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case PAW_EV_QUIT:
            return PAW_EVT_QUIT;
        case PAW_EV_KEY_DOWN:
            g_last_key = (int)PAW_KEY_OF(&e);
            return PAW_EVT_KEY_DOWN;
        case PAW_EV_KEY_UP:
            g_last_key = (int)PAW_KEY_OF(&e);
            return PAW_EVT_KEY_UP;
#if PAW_SDL3
        case SDL_EVENT_WINDOW_RESIZED:
            return PAW_EVT_WINDOW_RESIZED;
        case SDL_EVENT_MOUSE_MOTION:
            g_mouse_x = (int)e.motion.x;
            g_mouse_y = (int)e.motion.y;
            return PAW_EVT_MOUSE_MOTION;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            return PAW_EVT_MOUSE_DOWN;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return PAW_EVT_MOUSE_UP;
#else
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_RESIZED)
                return PAW_EVT_WINDOW_RESIZED;
            break;
        case SDL_MOUSEMOTION:
            g_mouse_x = e.motion.x;
            g_mouse_y = e.motion.y;
            return PAW_EVT_MOUSE_MOTION;
        case SDL_MOUSEBUTTONDOWN:
            return PAW_EVT_MOUSE_DOWN;
        case SDL_MOUSEBUTTONUP:
            return PAW_EVT_MOUSE_UP;
#endif
        default:
            break;                               /* uninteresting: keep polling */
        }
    }
    return PAW_EVT_NONE;
}

typedef enum { HDL_FREE = 0, HDL_WINDOW, HDL_RENDERER, HDL_TEXTURE } hdl_type_t;

typedef struct {
    hdl_type_t    type;
    void         *ptr;
    int           owner;      /* renderer -> window, texture -> renderer */
    unsigned char cr, cg, cb, ca;
} hdl_t;

#define FIRST_HANDLE 1

static hdl_t *g_h = NULL;
static int    g_cap = 0;
static int    g_initialized = 0;

static hdl_t *hdl_at(int id) {
    return (id >= FIRST_HANDLE && id < g_cap) ? &g_h[id] : NULL;
}

static int hdl_alloc(void) {
    if (!g_h) {
        g_cap = 64;
        g_h = (hdl_t *)calloc((size_t)g_cap, sizeof *g_h);
        if (!g_h) { g_cap = 0; return 0; }
    }
    for (int i = FIRST_HANDLE; i < g_cap; i++)
        if (g_h[i].type == HDL_FREE) return i;

    const int old_cap = g_cap;
    hdl_t *nh = (hdl_t *)realloc(g_h, (size_t)old_cap * 2 * sizeof *nh);
    if (!nh) return 0;
    memset(nh + old_cap, 0, (size_t)old_cap * sizeof *nh);
    g_h = nh;
    g_cap = old_cap * 2;
    return old_cap;
}

static int hdl_store(hdl_type_t type, void *ptr, int owner) {
    int id = hdl_alloc();
    if (id == 0) return 0;
    hdl_t *h = &g_h[id];
    h->type = type;
    h->ptr  = ptr;
    h->owner = owner;
    h->cr = h->cg = h->cb = 0;
    h->ca = 255;
    return id;
}

static void *hdl_get(int id, hdl_type_t type) {
    hdl_t *h = hdl_at(id);
    return (h && h->type == type) ? h->ptr : NULL;
}

static int module_init(int flags) {
    if (g_initialized) return 0;
    if (flags == 0) flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
    if (paw_init((Uint32)flags) != 0) return -1;
    g_initialized = 1;
    return 0;
}

static int ensure_init(void) {
    return g_initialized ? 0 : module_init(0);
}

static int module_create_window(const char *title, int width, int height) {
    if (ensure_init() != 0) return 0;
    if (width  <= 0) width  = 640;
    if (height <= 0) height = 480;
    if (!title || !*title) title = "paw window";

    SDL_Window *win = paw_create_window(title, width, height,
                                        PAW_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    return hdl_store(HDL_WINDOW, win, 0);
}

static int module_create_renderer(int window_id) {
    SDL_Window *win = (SDL_Window *)hdl_get(window_id, HDL_WINDOW);
    if (!win) return 0;
    SDL_Renderer *ren = paw_create_renderer(win, 1 /* vsync */);
    return hdl_store(HDL_RENDERER, ren, window_id);
}

static void module_set_draw_color(int ren_id, int r, int g, int b, int a) {
    hdl_t *h = hdl_at(ren_id);
    if (!h || h->type != HDL_RENDERER || !h->ptr) return;
    h->cr = (unsigned char)r;
    h->cg = (unsigned char)g;
    h->cb = (unsigned char)b;
    h->ca = (unsigned char)a;
    paw_set_draw_color((SDL_Renderer *)h->ptr, h->cr, h->cg, h->cb, h->ca);
}

static void module_render_clear(int ren_id) {
    SDL_Renderer *ren = (SDL_Renderer *)hdl_get(ren_id, HDL_RENDERER);
    if (ren) paw_clear(ren);
}

static void module_render_present(int ren_id) {
    SDL_Renderer *ren = (SDL_Renderer *)hdl_get(ren_id, HDL_RENDERER);
    if (ren) paw_present(ren);
}

static void module_draw_rect(int ren_id, int x, int y, int w, int h) {
    SDL_Renderer *ren = (SDL_Renderer *)hdl_get(ren_id, HDL_RENDERER);
    if (ren) paw_draw_rect(ren, x, y, w, h);
}

static void module_fill_rect(int ren_id, int x, int y, int w, int h) {
    SDL_Renderer *ren = (SDL_Renderer *)hdl_get(ren_id, HDL_RENDERER);
    if (ren) paw_fill_rect(ren, x, y, w, h);
}

static void module_draw_line(int ren_id, int x1, int y1, int x2, int y2) {
    SDL_Renderer *ren = (SDL_Renderer *)hdl_get(ren_id, HDL_RENDERER);
    if (ren) paw_draw_line(ren, x1, y1, x2, y2);
}

static int module_load_texture(int ren_id, const char *path) {
    SDL_Renderer *ren = (SDL_Renderer *)hdl_get(ren_id, HDL_RENDERER);
    if (!ren || !path) return 0;

    SDL_Surface *bmp = SDL_LoadBMP(path);
    if (!bmp) return 0;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, bmp);
    paw_free_surface(bmp);
    if (!tex) return 0;

    int id = hdl_store(HDL_TEXTURE, tex, ren_id);
    if (id == 0) SDL_DestroyTexture(tex);        /* table full: don't leak */
    return id;
}

static void module_draw_texture(int ren_id, int tex_id, int x, int y, int w, int h) {
    hdl_t *r = hdl_at(ren_id), *t = hdl_at(tex_id);
    if (!r || r->type != HDL_RENDERER || !r->ptr) return;
    if (!t || t->type != HDL_TEXTURE || !t->ptr) return;
    if (t->owner != ren_id) return;              /* texture belongs elsewhere */

    if (w <= 0 || h <= 0) {
        int nw = 0, nh = 0;
        if (paw_texture_size((SDL_Texture *)t->ptr, &nw, &nh) != 0) return;
        w = nw; h = nh;
    }
    paw_render_texture((SDL_Renderer *)r->ptr, (SDL_Texture *)t->ptr, x, y, w, h);
}

static int module_poll_event_pub(void) { return module_poll_event(); }
static int module_last_key(void)  { return g_last_key; }

static int module_key_down(int keycode) {
    int n = 0;
    const unsigned char *state = (const unsigned char *)SDL_GetKeyboardState(&n);
#if PAW_SDL3
    SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)keycode, NULL);
#else
    SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)keycode);
#endif
    return (sc != SDL_SCANCODE_UNKNOWN && (int)sc < n && state[sc]) ? 1 : 0;
}

static int module_mouse_x(void)     { int x, y; paw_query_mouse(&x, &y); return x; }
static int module_mouse_y(void)     { int x, y; paw_query_mouse(&x, &y); return y; }
static int module_mouse_down(int b) { return paw_mouse_down(b); }

static int module_window_width(int win_id) {
    SDL_Window *w = (SDL_Window *)hdl_get(win_id, HDL_WINDOW);
    int vw = 0, vh = 0;
    if (w) SDL_GetWindowSize(w, &vw, &vh);
    return vw;
}

static int module_window_height(int win_id) {
    SDL_Window *w = (SDL_Window *)hdl_get(win_id, HDL_WINDOW);
    int vw = 0, vh = 0;
    if (w) SDL_GetWindowSize(w, &vw, &vh);
    return vh;
}

static void module_set_window_title(int win_id, const char *title) {
    SDL_Window *w = (SDL_Window *)hdl_get(win_id, HDL_WINDOW);
    if (w && title) SDL_SetWindowTitle(w, title);
}

static void module_run_loop(int ren_id) {
    hdl_t *h = hdl_at(ren_id);
    if (!h || h->type != HDL_RENDERER || !h->ptr) return;
    SDL_Renderer *ren = (SDL_Renderer *)h->ptr;
    const int vsync = renderer_has_vsync(ren);

    for (;;) {
        if (module_poll_event() == PAW_EVT_QUIT) break;

        paw_set_draw_color(ren, h->cr, h->cg, h->cb, h->ca);
        paw_clear(ren);
        paw_present(ren);

        if (!vsync) SDL_Delay(16);               /* ~60 fps when vsync is off */
    }
}

static void module_delay(int ms) { SDL_Delay(ms); }
static int  module_ticks(void)   { return (int)SDL_GetTicks(); }

static void module_destroy_texture(int tex_id) {
    hdl_t *h = hdl_at(tex_id);
    if (!h || h->type != HDL_TEXTURE || !h->ptr) return;
    SDL_DestroyTexture((SDL_Texture *)h->ptr);
    h->type = HDL_FREE; h->ptr = NULL; h->owner = 0;
}

static void module_destroy_renderer(int ren_id) {
    hdl_t *h = hdl_at(ren_id);
    if (!h || h->type != HDL_RENDERER || !h->ptr) return;
    for (int i = FIRST_HANDLE; i < g_cap; i++)
        if (g_h[i].type == HDL_TEXTURE && g_h[i].owner == ren_id)
            module_destroy_texture(i);
    SDL_DestroyRenderer((SDL_Renderer *)h->ptr);
    h->type = HDL_FREE; h->ptr = NULL; h->owner = 0;
}

static void module_destroy_window(int win_id) {
    hdl_t *h = hdl_at(win_id);
    if (!h || h->type != HDL_WINDOW || !h->ptr) return;
    for (int i = FIRST_HANDLE; i < g_cap; i++)
        if (g_h[i].type == HDL_RENDERER && g_h[i].owner == win_id)
            module_destroy_renderer(i);
    SDL_DestroyWindow((SDL_Window *)h->ptr);
    h->type = HDL_FREE; h->ptr = NULL; h->owner = 0;
}

static void module_quit(void) {
    if (g_h) {
        for (int i = FIRST_HANDLE; i < g_cap; i++)
            if (g_h[i].type == HDL_TEXTURE)  module_destroy_texture(i);
        for (int i = FIRST_HANDLE; i < g_cap; i++)
            if (g_h[i].type == HDL_RENDERER) module_destroy_renderer(i);
        for (int i = FIRST_HANDLE; i < g_cap; i++)
            if (g_h[i].type == HDL_WINDOW)   module_destroy_window(i);
        free(g_h);
        g_h = NULL;
        g_cap = 0;
    }
    if (g_initialized) { SDL_Quit(); g_initialized = 0; }
    g_last_key = g_mouse_x = g_mouse_y = 0;
}

static const char *module_get_error(void) { return SDL_GetError(); }

static const paw_ffi_function_t functions[] = {
    { .name = "init",             .address = (void *)module_init,               .return_type = PAW_FFI_INT,     .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "create_window",    .address = (void *)module_create_window,      .return_type = PAW_FFI_INT,     .arg_count = 3, .args = { PAW_FFI_CSTRING, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "create_renderer",  .address = (void *)module_create_renderer,    .return_type = PAW_FFI_INT,     .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "set_draw_color",   .address = (void *)module_set_draw_color,     .return_type = PAW_FFI_VOID,    .arg_count = 5, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "clear",            .address = (void *)module_render_clear,       .return_type = PAW_FFI_VOID,    .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "present",          .address = (void *)module_render_present,     .return_type = PAW_FFI_VOID,    .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "draw_rect",        .address = (void *)module_draw_rect,          .return_type = PAW_FFI_VOID,    .arg_count = 5, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "fill_rect",        .address = (void *)module_fill_rect,          .return_type = PAW_FFI_VOID,    .arg_count = 5, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "draw_line",        .address = (void *)module_draw_line,          .return_type = PAW_FFI_VOID,    .arg_count = 5, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "run_loop",         .address = (void *)module_run_loop,           .return_type = PAW_FFI_VOID,    .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "delay",            .address = (void *)module_delay,              .return_type = PAW_FFI_VOID,    .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "destroy_window",   .address = (void *)module_destroy_window,     .return_type = PAW_FFI_VOID,    .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "destroy_renderer", .address = (void *)module_destroy_renderer,   .return_type = PAW_FFI_VOID,    .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "quit",             .address = (void *)module_quit,               .return_type = PAW_FFI_VOID,    .arg_count = 0, .args = { 0 } },
    { .name = "get_error",        .address = (void *)module_get_error,          .return_type = PAW_FFI_CSTRING, .arg_count = 0, .args = { 0 } },
    { .name = "poll_event",       .address = (void *)module_poll_event_pub,     .return_type = PAW_FFI_INT,     .arg_count = 0, .args = { 0 } },
    { .name = "last_key",         .address = (void *)module_last_key,           .return_type = PAW_FFI_INT,     .arg_count = 0, .args = { 0 } },
    { .name = "key_down",         .address = (void *)module_key_down,           .return_type = PAW_FFI_INT,     .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "mouse_x",          .address = (void *)module_mouse_x,            .return_type = PAW_FFI_INT,     .arg_count = 0, .args = { 0 } },
    { .name = "mouse_y",          .address = (void *)module_mouse_y,            .return_type = PAW_FFI_INT,     .arg_count = 0, .args = { 0 } },
    { .name = "mouse_down",       .address = (void *)module_mouse_down,         .return_type = PAW_FFI_INT,     .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "window_width",     .address = (void *)module_window_width,       .return_type = PAW_FFI_INT,     .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "window_height",    .address = (void *)module_window_height,      .return_type = PAW_FFI_INT,     .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "set_window_title", .address = (void *)module_set_window_title,   .return_type = PAW_FFI_VOID,    .arg_count = 2, .args = { PAW_FFI_INT, PAW_FFI_CSTRING } },
    { .name = "load_texture",     .address = (void *)module_load_texture,       .return_type = PAW_FFI_INT,     .arg_count = 2, .args = { PAW_FFI_INT, PAW_FFI_CSTRING } },
    { .name = "draw_texture",     .address = (void *)module_draw_texture,       .return_type = PAW_FFI_VOID,    .arg_count = 6, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "destroy_texture",  .address = (void *)module_destroy_texture,    .return_type = PAW_FFI_VOID,    .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "ticks",            .address = (void *)module_ticks,              .return_type = PAW_FFI_INT,     .arg_count = 0, .args = { 0 } },
};

static const paw_library_t library = {
    .abi_version    = PAW_FFI_ABI_VERSION,
    .name           = "sdl",
    .function_count = sizeof(functions) / sizeof(functions[0]),
    .functions      = functions
};

const paw_library_t *paw_library_info(void) {
    return &library;
}
