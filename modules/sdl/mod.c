#include "pawffi.h"
#include <stdio.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#define MAX_HANDLES 64

typedef enum {
    HANDLE_FREE = 0,
    HANDLE_WINDOW,
    HANDLE_RENDERER
} handle_type_t;

typedef struct {
    handle_type_t type;
    void *ptr;
} handle_slot_t;

static handle_slot_t handles[MAX_HANDLES];

static int store_handle(handle_type_t type, void *ptr) {
    if (!ptr) return 0;
    for (int i = 1; i < MAX_HANDLES; i++) {
        if (handles[i].type == HANDLE_FREE) {
            handles[i].type = type;
            handles[i].ptr = ptr;
            return i;
        }
    }
    return 0;
}

static void *get_handle(int id, handle_type_t type) {
    if (id <= 0 || id >= MAX_HANDLES) return NULL;
    if (handles[id].type != type) return NULL;
    return handles[id].ptr;
}

static void free_handle(int id) {
    if (id > 0 && id < MAX_HANDLES) {
        handles[id].type = HANDLE_FREE;
        handles[id].ptr = NULL;
    }
}

static int module_init(int flags) {
    if (flags == 0) flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
    return SDL_Init((uint32_t)flags) == 0 ? 0 : -1;
}

static int module_create_window(const char *title, int width, int height) {
    SDL_Window *win = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    return store_handle(HANDLE_WINDOW, win);
}

static int module_create_renderer(int window_id) {
    SDL_Window *win = (SDL_Window *)get_handle(window_id, HANDLE_WINDOW);
    if (!win) return 0;
    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    return store_handle(HANDLE_RENDERER, ren);
}

static void module_set_draw_color(int renderer_id, int r, int g, int b, int a) {
    SDL_Renderer *ren = (SDL_Renderer *)get_handle(renderer_id, HANDLE_RENDERER);
    if (ren) SDL_SetRenderDrawColor(ren, (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
}

static void module_render_clear(int renderer_id) {
    SDL_Renderer *ren = (SDL_Renderer *)get_handle(renderer_id, HANDLE_RENDERER);
    if (ren) SDL_RenderClear(ren);
}

static void module_render_present(int renderer_id) {
    SDL_Renderer *ren = (SDL_Renderer *)get_handle(renderer_id, HANDLE_RENDERER);
    if (ren) SDL_RenderPresent(ren);
}

static void module_draw_rect(int renderer_id, int x, int y, int w, int h) {
    SDL_Renderer *ren = (SDL_Renderer *)get_handle(renderer_id, HANDLE_RENDERER);
    if (!ren) return;
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(ren, &rect);
}

static void module_fill_rect(int renderer_id, int x, int y, int w, int h) {
    SDL_Renderer *ren = (SDL_Renderer *)get_handle(renderer_id, HANDLE_RENDERER);
    if (!ren) return;
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(ren, &rect);
}

static void module_draw_line(int renderer_id, int x1, int y1, int x2, int y2) {
    SDL_Renderer *ren = (SDL_Renderer *)get_handle(renderer_id, HANDLE_RENDERER);
    if (!ren) return;
    SDL_RenderDrawLine(ren, x1, y1, x2, y2);
}

static void module_run_loop(int renderer_id) {
    SDL_Renderer *renderer = (SDL_Renderer *)get_handle(renderer_id, HANDLE_RENDERER);
    if (!renderer) return;

    SDL_Event event;
    int running = 1;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
        }

        SDL_SetRenderDrawColor(renderer, 30, 34, 42, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }
}

static void module_destroy_window(int window_id) {
    SDL_Window *win = (SDL_Window *)get_handle(window_id, HANDLE_WINDOW);
    if (win) {
        SDL_DestroyWindow(win);
        free_handle(window_id);
    }
}

static void module_destroy_renderer(int renderer_id) {
    SDL_Renderer *ren = (SDL_Renderer *)get_handle(renderer_id, HANDLE_RENDERER);
    if (ren) {
        SDL_DestroyRenderer(ren);
        free_handle(renderer_id);
    }
}

static void module_quit(void) {
    for (int i = 1; i < MAX_HANDLES; i++) {
        if (handles[i].type == HANDLE_RENDERER) {
            SDL_DestroyRenderer((SDL_Renderer *)handles[i].ptr);
        } else if (handles[i].type == HANDLE_WINDOW) {
            SDL_DestroyWindow((SDL_Window *)handles[i].ptr);
        }
        handles[i].type = HANDLE_FREE;
    }
    SDL_Quit();
}

static const paw_ffi_function_t functions[] = {
    { .name = "init", .address = (void *)module_init, .return_type = PAW_FFI_INT, .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "create_window", .address = (void *)module_create_window, .return_type = PAW_FFI_INT, .arg_count = 3, .args = { PAW_FFI_CSTRING, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "create_renderer", .address = (void *)module_create_renderer, .return_type = PAW_FFI_INT, .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "set_draw_color", .address = (void *)module_set_draw_color, .return_type = PAW_FFI_VOID, .arg_count = 5, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "clear", .address = (void *)module_render_clear, .return_type = PAW_FFI_VOID, .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "present", .address = (void *)module_render_present, .return_type = PAW_FFI_VOID, .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "draw_rect", .address = (void *)module_draw_rect, .return_type = PAW_FFI_VOID, .arg_count = 5, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "fill_rect", .address = (void *)module_fill_rect, .return_type = PAW_FFI_VOID, .arg_count = 5, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "draw_line", .address = (void *)module_draw_line, .return_type = PAW_FFI_VOID, .arg_count = 5, .args = { PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT, PAW_FFI_INT } },
    { .name = "run_loop", .address = (void *)module_run_loop, .return_type = PAW_FFI_VOID, .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "destroy_window", .address = (void *)module_destroy_window, .return_type = PAW_FFI_VOID, .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "destroy_renderer", .address = (void *)module_destroy_renderer, .return_type = PAW_FFI_VOID, .arg_count = 1, .args = { PAW_FFI_INT } },
    { .name = "quit", .address = (void *)module_quit, .return_type = PAW_FFI_VOID, .arg_count = 0, .args = { 0 } }
};

static const paw_library_t library = {
    .abi_version = PAW_FFI_ABI_VERSION,
    .name = "sdl",
    .function_count = sizeof(functions) / sizeof(functions[0]),
    .functions = functions
};

const paw_library_t *paw_library_info(void) {
    return &library;
}
