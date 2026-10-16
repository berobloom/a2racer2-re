/* a2menu -- the A2 Racer II front end (menu.dat), rebuilt.
 *
 *   a2menu <datadir> [--race <a2view>] [--no-intro]
 *          [--shot <screen> <out.ppm> [--click x y]...]
 *
 * It mirrors the original's split.  The menu runs; START saves the slot,
 * writes Game.ini (0x401cc0) and runs the race -- a2view standing in for
 * spel.dat, as a2_II.exe runs it after the menu exits with 40 -- and then
 * reads the race's results back exactly as menu.dat does on start-up
 * (0x409b87).  Layouts come from menu.dat's own form tables (mdata.c); art and
 * fonts from Mrcs.img / MENURC.IMG (massets.c).  See
 * reverse-engineering/menu.md.
 *
 * --shot renders one screen with its animations finished, for comparing with
 * captures of the original (reverse-engineering/tools/menutools.py diff).
 */
#include "menu.h"
#include "msmacker.h"

#include "../a2_io.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef A2_WEB
#include <sys/wait.h>
#include <unistd.h>
#endif

static int write_ppm(const char *path, const m_canvas *c, const uint8_t pal[768])
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", M_W, M_H);
    for (int i = 0; i < M_W * M_H; i++)
        fwrite(pal + c->px[i] * 3, 1, 3, f);
    return fclose(f);
}

/* Where the menu's 640x480 picture goes.  The desktop uses an SDL renderer.
 * The web build draws it through GL instead: a canvas has one WebGL context,
 * which the race's fixed-function emulation shares, and SDL's renderer
 * misdrew on it after a race (half the screen). */
#ifdef A2_WEB
#include <GL/gl.h>
typedef struct {
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint tex;
} screen;

static void screen_open(screen *s, SDL_Window *win)
{
    s->win = win;
    s->ctx = SDL_GL_CreateContext(win);
    a2web_gl_reset();
    glGenTextures(1, &s->tex);
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* 1024x512 holds the 640x480 picture at a power of two, pixel for pixel */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

static void screen_show(screen *s, const uint8_t *rgba)
{
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(s->win, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBindTexture(GL_TEXTURE_2D, s->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, M_W, M_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
    float u = M_W / 1024.0f, v = M_H / 512.0f;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, 1);
    glTexCoord2f(u, 0); glVertex2f(1, 1);
    glTexCoord2f(u, v); glVertex2f(1, -1);
    glTexCoord2f(0, v); glVertex2f(-1, -1);
    glEnd();
    SDL_GL_SwapWindow(s->win);
}

static void screen_close(screen *s)
{
    glDeleteTextures(1, &s->tex);
    SDL_GL_DeleteContext(s->ctx);
}
#else
typedef struct {
    SDL_Renderer *ren;
    SDL_Texture *tex;
} screen;

static void screen_open(screen *s, SDL_Window *win)
{
    s->ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(s->ren, M_W, M_H);
    s->tex = SDL_CreateTexture(s->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
                               M_W, M_H);
}

static void screen_show(screen *s, const uint8_t *rgba)
{
    SDL_UpdateTexture(s->tex, NULL, rgba, M_W * 4);
    SDL_RenderClear(s->ren);
    SDL_RenderCopy(s->ren, s->tex, NULL, NULL);
    SDL_RenderPresent(s->ren);
}

static void screen_close(screen *s)
{
    SDL_DestroyTexture(s->tex);
    SDL_DestroyRenderer(s->ren);
}
#endif

/* Play <name>.smk from the data dir (menu.dat 0x409f70): Raser.ini [MOVIES]
 * ON=0 skips them, SIZE=1 doubles the 320x240 picture to the full screen,
 * SIZE=0 shows it centred.  A key or a click ends it. */
static void play_video(const menu *m, screen *scr, const char *name)
{
    if (!a2_ini_int(m->datadir, "Raser.ini", "[MOVIES]", "ON", 1))
        return;
    int full = a2_ini_int(m->datadir, "Raser.ini", "[MOVIES]", "SIZE", 1);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.smk", m->datadir, name);
    size_t size = 0;
    uint8_t *data = a2_read_file(path, &size);
    smk *v = data ? smk_open(data, size) : NULL;
    if (!v) {
        free(data);
        return;
    }
    int rate, ch, bits;
    smk_audio_info(v, &rate, &ch, &bits);
    SDL_AudioDeviceID dev = 0;
    if (rate && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want = {0};
        want.freq = rate;
        want.format = bits == 16 ? AUDIO_S16LSB : AUDIO_U8;
        want.channels = (Uint8)ch;
        want.samples = 1024;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (dev)
            SDL_PauseAudioDevice(dev, 0);
    }
    static uint8_t rgba[M_W * M_H * 4];
    int w = smk_width(v), h = smk_height(v), scale = full ? 2 : 1;
    int ox = (M_W - w * scale) / 2, oy = (M_H - h * scale) / 2;
    Uint32 start = SDL_GetTicks();
    int stop = 0;
    for (int f = 0; !stop && smk_next(v) == 0; f++) {
        size_t ab;
        const uint8_t *a = smk_audio(v, &ab);
        if (dev && ab)
            SDL_QueueAudio(dev, a, (Uint32)ab);
        memset(rgba, 0, sizeof rgba);
        const uint8_t *px = smk_pixels(v), *pal = smk_palette(v);
        for (int y = 0; y < h * scale; y++)
            for (int x = 0; x < w * scale; x++) {
                const uint8_t *c = pal + 3 * px[(y / scale) * w + x / scale];
                uint8_t *o = rgba + 4 * ((oy + y) * M_W + ox + x);
                o[0] = c[0];
                o[1] = c[1];
                o[2] = c[2];
                o[3] = 255;
            }
        screen_show(scr, rgba);
        Uint32 due = start + (Uint32)((f + 1) * smk_frame_ms(v));
        while (!stop && SDL_GetTicks() < due) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
                if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_QUIT)
                    stop = 1;
            SDL_Delay(5);
        }
    }
    if (dev)
        SDL_CloseAudioDevice(dev);
    smk_close(v);
    free(data);
}

/* Run the race and wait for it, as a2_II.exe does with spel.dat. */
#ifdef A2_WEB
/* The web build has no processes: the race is a call (src/main.c), which
 * ends with SDL_Quit, so the menu brings SDL back up for itself. */
int a2view_main(int argc, char **argv);
static int run_race(const menu *m)
{
    char stage[32];
    snprintf(stage, sizeof stage, "etappe%d", m_slot_int(&m->career, M_SLOT_ETAPPE));
    fprintf(stderr, "menu: starting the race %s %s\n", m->datadir, stage);
    char *argv[] = {"a2view", (char *)m->datadir, stage, "--game", NULL};
    int r = a2view_main(4, argv);
    a2web_gl_reset(); /* the race's GL state off the canvas's one context */
    SDL_Init(SDL_INIT_VIDEO);
    return r;
}
#else
static int run_race(const menu *m)
{
    char stage[32];
    snprintf(stage, sizeof stage, "etappe%d", m_slot_int(&m->career, M_SLOT_ETAPPE));
    fprintf(stderr, "menu: starting %s %s %s\n", m->race_cmd, m->datadir, stage);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execl(m->race_cmd, m->race_cmd, m->datadir, stage, "--game", (char *)NULL);
        perror(m->race_cmd);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

/* What the menu does when it (re)starts: load the slots, take a finished
 * race's results, read the device. */
static void load_state(menu *m, int read_only)
{
    m_career_load(&m->career, m->datadir);
    m_career_take_results(&m->career, m->datadir, !read_only);
    m->device = a2_ini_int(m->datadir, "Game.ini", "[INPUT]", "DEVICE", 1);
    if (m->device < 1 || m->device > 4)
        m->device = 1;
}

int main(int argc, char **argv)
{
    int slide_step = -1;
    if (argc < 2) {
        fprintf(stderr, "usage: a2menu <datadir> [--race <a2view>] [--shot <screen> <out.ppm>]\n");
        return 1;
    }
    static menu m;
    m.datadir = argv[1];
    const char *shot_screen = NULL, *shot_path = NULL;
    int clicks[16][2], nclicks = 0; /* --click x y: replayed before a --shot */
    int no_intro = 0, first = 1;
    static char race_default[1024];
    snprintf(race_default, sizeof race_default, "%s", argv[0]);
    char *slash = strrchr(race_default, '/');
    snprintf(slash ? slash + 1 : race_default,
             sizeof race_default - (size_t)(slash ? slash + 1 - race_default : 0), "a2view");
    m.race_cmd = race_default;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 2 < argc) {
            shot_screen = argv[++i];
            shot_path = argv[++i];
        } else if (strcmp(argv[i], "--click") == 0 && i + 2 < argc && nclicks < 16) {
            clicks[nclicks][0] = atoi(argv[++i]);
            clicks[nclicks++][1] = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--slide-step") == 0 && i + 1 < argc) {
            slide_step = atoi(argv[++i]); /* dev: freeze a showroom slide */
        } else if (strcmp(argv[i], "--no-intro") == 0) {
            no_intro = 1;
        } else if (strcmp(argv[i], "--race") == 0 && i + 1 < argc) {
            m.race_cmd = argv[++i];
        }
    }
    if (m_assets_open(&m.a, m.datadir) != 0 || m_data_load(&m.d, m.datadir) != 0)
        return 1;
    m.track_count = a2_trackinfo_load(m.tracks, A2_MAX_TRACKS, m.datadir);
    m_garage_load(&m);
    m_options_load(&m);
    load_state(&m, shot_screen != NULL);
    m.next_screen = -1;
    m.show_slide = -1;

    static m_canvas canvas;
    if (shot_screen) {
        int s = SCR_MAIN;
        for (int i = 0; i < SCR_COUNT; i++)
            if (strcmp(m_screens[i].name, shot_screen) == 0)
                s = i;
        m.car_frame = 5;
        m_ui_enter(&m, s);
        m_ui_finish(&m);
        for (int k = 0; k < nclicks; k++) {
            m_ui_mouse_move(&m, clicks[k][0], clicks[k][1]);
            m_ui_mouse_button(&m, 1);
            m_ui_mouse_button(&m, 0);
            if (m.next_screen >= 0) {
                m_ui_enter(&m, m.next_screen);
                m_ui_finish(&m);
                m.next_screen = -1;
            }
        }
        m_ui_mouse_move(&m, -100, -100);
        m_ui_draw(&m, &canvas, 0); /* screens set their animated text while drawing */
        int slide_to = m.show_slide_to, sliding = m.show_slide >= 0;
        m_ui_finish(&m);
        if (sliding && slide_step >= 0) { /* back to the old car, mid-slide */
            m.show_cand = (slide_to + (m.show_slide_dir ? 5 : 1)) % 6;
            m.show_slide = slide_step;
            m.show_slide_last = m.now = 0xffffff00u;
        }
        m_ui_draw(&m, &canvas, 0);
        return write_ppm(shot_path, &canvas, m.a.pal) == 0 ? 0 : 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    static uint8_t rgba[M_W * M_H * 4];
    for (;;) {
        m.audio = m_audio_open(&m.a);
        Uint32 flags = SDL_WINDOW_RESIZABLE;
#ifdef A2_WEB
        flags |= SDL_WINDOW_OPENGL;
#endif
        if (getenv("A2_FULLSCREEN") && atoi(getenv("A2_FULLSCREEN")))
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP; /* the race takes it too */
        SDL_Window *win = SDL_CreateWindow("A2 Racer II", SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED, M_W * 2, M_H * 2, flags);
        screen scr;
        screen_open(&scr, win);
        SDL_ShowCursor(SDL_DISABLE);
        SDL_StopTextInput();
        /* \FIRST: the shell starts the menu with it once, and the intro plays */
        if (first && !no_intro)
            play_video(&m, &scr, "intro");
        first = 0;
        /* After the last stage the menu loop first shows the standings
         * (0x4076c7 -> 0x405630, which opens mhigh_open_screen 0x405530) and
         * then plays the ending (0x407580): win.smk if the player leads the
         * overall standings (row 0 of TOTAAL by name), lose.smk otherwise,
         * and the career goes back to stage 0.  Here the video comes as the
         * standings are left. */
        int career_end = m.career.finished_career;
        m.now = SDL_GetTicks();
        m.quit = m.start_race = 0;
        memset(m.kept_focus, 0, sizeof m.kept_focus); /* a fresh menu.dat */
        free(m.bg);
        m.bg = NULL;
        m_ui_enter(&m, career_end ? SCR_STANDINGS : SCR_MAIN);
        while (!m.quit && !m.start_race) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT)
                    m.quit = 1;
                else if (ev.type == SDL_MOUSEMOTION) {
#ifdef A2_WEB
                    /* the desktop's renderer maps the window onto the 640x480
                     * screen (SDL_RenderSetLogicalSize); the web build's GL
                     * picture fills the canvas, so scale here */
                    {
                        int ww = 1, wh = 1;
                        SDL_GetWindowSize(win, &ww, &wh);
                        ev.motion.x = ev.motion.x * M_W / (ww > 0 ? ww : 1);
                        ev.motion.y = ev.motion.y * M_H / (wh > 0 ? wh : 1);
                    }
#endif
                    m_ui_mouse_move(&m, ev.motion.x, ev.motion.y);
                    if (m.mouse_down && m.screen == SCR_OPTIONS && !m.req.active)
                        m_options_drag(&m, ev.motion.x, ev.motion.y);
                    if (m.mouse_down && m.screen == SCR_CONTROLS && !m.req.active)
                        m_controls_drag(&m, ev.motion.x, ev.motion.y);
                } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    m.mouse_down = 1;
                    m_ui_mouse_button(&m, 1);
                } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                    m.mouse_down = 0;
                    m_ui_mouse_button(&m, 0);
                }
                else if (ev.type == SDL_KEYDOWN)
                    m_ui_key(&m, ev.key.keysym.sym);
                else if (ev.type == SDL_TEXTINPUT && m.screen == SCR_NEWPLAYER)
                    m_newplayer_text(&m, ev.text.text);
            }
            m.now = SDL_GetTicks();
            m_ui_tick(&m);
            if (m.next_screen >= 0) {
                if (career_end && m.screen == SCR_STANDINGS) {
                    const uint8_t *lead = m_slot_row(&m.career, 0, 0);
                    int won = strncmp((const char *)lead + 1, m_slot_name(&m.career), 9) == 0;
                    play_video(&m, &scr, won ? "win" : "lose");
                    m_slot_set_int(&m.career, M_SLOT_ETAPPE, 0);
                    career_end = 0;
                }
                m_ui_enter(&m, m.next_screen);
                m.next_screen = -1;
            }
            m_ui_draw(&m, &canvas, 1);
            m_to_rgba(&canvas, m.a.pal, rgba);
            screen_show(&scr, rgba);
            SDL_Delay(10);
        }
        if (m.quit)
            play_video(&m, &scr, "exit"); /* the menu returned 0xff (0x409bbe) */
        screen_close(&scr);
        SDL_DestroyWindow(win);
        m_audio_close(m.audio); /* the race has the sound device to itself */
        m.audio = NULL;
        if (m.quit)
            break;
        /* START: save, hand over to the race, come back as a fresh menu */
        m_career_save(&m.career, m.datadir);
        m_options_write_ini(&m); /* 0x401cc0 starts with ini_write_options */
        m_career_write_race(&m.career, m.datadir);
        run_race(&m);
        load_state(&m, 0);
    }
    m_career_save(&m.career, m.datadir);
    SDL_Quit();
    return 0;
}
