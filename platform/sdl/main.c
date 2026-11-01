/* mbsim - desktop simulator.
 *
 * Renders through exactly the same strip path the microcontroller uses, at any
 * display size, and hot-reloads the pack whenever the .mbp file changes (run
 * `packc --watch` alongside it).
 *
 *   mbsim build/forest.mbp [--size 320x240] [--zoom 3] [--seed N]
 *
 * Keys: arrows pan | Z/X = A/B buttons | T cycle time speed (1x/60x/600x)
 *       [ ] hour -/+ | W cycle weather | F1 debug overlay | R reload | Esc quit
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "mb/mb.h"

static mb_engine_t eng;

static bool host_wall_time(void *ctx, mb_datetime_t *out) {
    (void)ctx;
    time_t t = time(NULL);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    out->year = (uint16_t)(tm.tm_year + 1900);
    out->month = (uint8_t)(tm.tm_mon + 1);
    out->day = (uint8_t)tm.tm_mday;
    out->hour = (uint8_t)tm.tm_hour;
    out->minute = (uint8_t)tm.tm_min;
    out->second = (uint8_t)tm.tm_sec;
    return true;
}

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static time_t mtime_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

typedef struct {
    const char *path;
    unsigned char *data;
    mb_config_t cfg;
    time_t mtime;
} pack_state_t;

/* (Re)load the pack, keeping camera and clock where they were on reload. */
static bool load_pack(pack_state_t *ps, bool keep_view) {
    size_t size;
    unsigned char *data = read_file(ps->path, &size);
    if (!data) { fprintf(stderr, "mbsim: cannot read %s\n", ps->path); return false; }
    int32_t cam_x = eng.cam_x, cam_y = eng.cam_y;
    uint32_t minute = mb_day_minute(&eng), scale = eng.time_scale;
    bool wall = eng.clock_wall;
    mb_err_t err = mb_engine_init(&eng, data, size, &ps->cfg);
    if (err) {
        fprintf(stderr, "mbsim: %s: %s\n", ps->path, mb_err_str(err));
        free(data);
        if (ps->data) mb_engine_init(&eng, ps->data, size, &ps->cfg); /* keep the old one */
        return false;
    }
    free(ps->data);
    ps->data = data;
    ps->mtime = mtime_of(ps->path);
    if (keep_view) {
        eng.cam_x = cam_x;
        eng.cam_y = cam_y;
        if (!wall) mb_set_day_minute(&eng, minute);
        mb_set_time_scale(&eng, scale);
    }
    printf("mbsim: loaded %s (viewport %ux%u at %ux)\n", ps->path, eng.view_w, eng.view_h, eng.scale);
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: mbsim PACK.mbp [--size WxH] [--zoom N] [--seed N]\n");
        return 2;
    }
    pack_state_t ps = {.path = argv[1]};
    ps.cfg = (mb_config_t){.width = 320, .height = 240, .format = MB_PIXFMT_XRGB8888,
                           .seed = (uint32_t)time(NULL), .warmup_s = 90,
                           .host = {.wall_time = host_wall_time}};
    int zoom = 3;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--size")) {
            unsigned w, h;
            if (sscanf(argv[i + 1], "%ux%u", &w, &h) == 2) { ps.cfg.width = (uint16_t)w; ps.cfg.height = (uint16_t)h; }
        } else if (!strcmp(argv[i], "--zoom")) {
            zoom = atoi(argv[i + 1]);
        } else if (!strcmp(argv[i], "--seed")) {
            ps.cfg.seed = (uint32_t)strtoul(argv[i + 1], NULL, 0);
        }
    }
    if (!load_pack(&ps, false)) return 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    int W = ps.cfg.width, H = ps.cfg.height;
    SDL_Window *win = SDL_CreateWindow("microbiome", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       W * zoom, H * zoom, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, W, H);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, W, H);

    static const uint32_t speeds[] = {1, 60, 600};
    int speed_i = 0, weather = mb_var_find(&eng, "weather"), weather_pin = -1;
    bool overlay = false, running = true;
    uint32_t last = SDL_GetTicks(), title_ms = 0, reload_ms = 0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type != SDL_KEYDOWN) continue;
            switch (ev.key.keysym.sym) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_t:
                speed_i = (speed_i + 1) % 3;
                mb_set_time_scale(&eng, speeds[speed_i]);
                break;
            case SDLK_LEFTBRACKET: mb_set_day_minute(&eng, (mb_day_minute(&eng) + 1440 - 60) % 1440); break;
            case SDLK_RIGHTBRACKET: mb_set_day_minute(&eng, (mb_day_minute(&eng) + 60) % 1440); break;
            case SDLK_w:
                if (weather >= 0) {
                    int n = mb_var_enum_count(&eng, weather);
                    weather_pin = weather_pin + 1 >= n ? -1 : weather_pin + 1;
                    if (weather_pin < 0) mb_var_unpin(&eng, weather);
                    else mb_var_pin(&eng, weather, weather_pin);
                }
                break;
            case SDLK_F1: overlay = !overlay; break;
            case SDLK_r: load_pack(&ps, true); break;
            default: break;
            }
        }

        const Uint8 *k = SDL_GetKeyboardState(NULL);
        uint32_t in = 0;
        if (k[SDL_SCANCODE_LEFT]) in |= MB_IN_LEFT;
        if (k[SDL_SCANCODE_RIGHT]) in |= MB_IN_RIGHT;
        if (k[SDL_SCANCODE_UP]) in |= MB_IN_UP;
        if (k[SDL_SCANCODE_DOWN]) in |= MB_IN_DOWN;
        if (k[SDL_SCANCODE_Z]) in |= MB_IN_A;
        if (k[SDL_SCANCODE_X]) in |= MB_IN_B;

        uint32_t now = SDL_GetTicks(), dt = now - last;
        last = now;
        mb_engine_update(&eng, dt, in);

        /* hot reload when packc rewrites the file */
        if ((reload_ms += dt) >= 500) {
            reload_ms = 0;
            time_t mt = mtime_of(ps.path);
            if (mt && mt != ps.mtime) {
                load_pack(&ps, true);
                weather = mb_var_find(&eng, "weather");
                weather_pin = -1;
            }
        }

        void *pixels;
        int pitch;
        SDL_LockTexture(tex, NULL, &pixels, &pitch);
        for (int y = 0; y < H; y += 16)
            mb_engine_render_strip(&eng, y, (H - y) < 16 ? H - y : 16, (uint8_t *)pixels + (size_t)y * pitch,
                                   (size_t)pitch);
        SDL_UnlockTexture(tex);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);

        if (overlay) {
            SDL_SetRenderDrawColor(ren, 255, 0, 255, 255);
            for (int i = 0; i < mb_actor_count(&eng); i++) {
                mb_actor_info_t ai;
                if (!mb_actor_info(&eng, i, &ai)) continue;
                SDL_Rect r = {ai.sx, ai.sy, ai.w, ai.h};
                SDL_RenderDrawRect(ren, &r);
            }
            SDL_SetRenderDrawColor(ren, 0, 255, 255, 255);
            for (uint32_t i = 0; i < eng.n_places; i++) {
                int x, y;
                mb_world_to_display(&eng, eng.places[i].x, eng.places[i].y, &x, &y);
                SDL_Rect r = {x - 2, y - 2, 5, 5};
                SDL_RenderFillRect(ren, &r);
            }
            SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
            for (uint32_t i = 0; i < eng.n_lanes; i++) {
                int x0, y0, x1, y1;
                mb_world_to_display(&eng, 0, eng.lanes[i].y_min, &x0, &y0);
                mb_world_to_display(&eng, eng.world->width, eng.lanes[i].y_max, &x1, &y1);
                SDL_RenderDrawLine(ren, x0, y0, x1, y0);
                SDL_RenderDrawLine(ren, x0, y1, x1, y1);
            }
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        }
        SDL_RenderPresent(ren);

        if ((title_ms += dt) >= 250) {
            title_ms = 0;
            char title[512], lbl[48];
            int n = snprintf(title, sizeof title, "microbiome  %02u:%02u x%u  actors %d ",
                             mb_day_minute(&eng) / 60, mb_day_minute(&eng) % 60, speeds[speed_i],
                             mb_actor_count(&eng));
            for (int i = 4; i < (int)eng.n_vars && n < (int)sizeof title - 40; i++)
                n += snprintf(title + n, sizeof title - (size_t)n, " %s=%s", mb_var_name(&eng, i),
                              mb_var_label(&eng, i, lbl, sizeof lbl));
            if (overlay) {
                for (int i = 0; i < mb_actor_count(&eng) && n < (int)sizeof title - 40; i++) {
                    mb_actor_info_t ai;
                    if (mb_actor_info(&eng, i, &ai))
                        n += snprintf(title + n, sizeof title - (size_t)n, " | %s:%s", ai.name, ai.state);
                }
            }
            SDL_SetWindowTitle(win, title);
        }
        if (dt < 16) SDL_Delay(16 - dt);
    }
    SDL_Quit();
    free(ps.data);
    return 0;
}
