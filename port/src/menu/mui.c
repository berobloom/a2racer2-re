#include "menu.h"

#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#define STYLE_BIG 0x4274a8u

int m_anim_frame(const menu *m, int anim, int frame)
{
    if (anim < 0 || anim >= m->d.anim_count || m->d.anim[anim].frames <= 0)
        return -1;
    const m_anim *an = &m->d.anim[anim];
    if (frame < 0)
        frame = 0;
    return an->sprite[frame % an->frames];
}

void m_draw_sprite(menu *m, m_canvas *c, int index, int x, int y)
{
    m_sprite s;
    if (index >= 0 && m_sprite_get(&m->a, index, &s) == 0)
        m_blit(c, &s, x, y);
}

void m_text_centred(m_canvas *c, const m_font *f, int x1, int x2, int y, const char *s)
{
    m_text(c, f, x1 + ((x2 - x1 + 1) - m_text_width(f, s)) / 2, y, s);
}

/* ---- the yes/no requester (janeereq.cpp, 0x402430 / 0x402680) ---------- */

#define REQ_X 119 /* (640 - 401) / 2: panel sprite 150 (anim 56), 401x222 */
#define REQ_Y 129 /* (480 - 222) / 2 */

void m_request_open(menu *m, const char *title, const char *text,
                    void (*done)(menu *m, int yes), int arg)
{
    m_request *r = &m->req;
    r->active = 1;
    snprintf(r->title, sizeof r->title, "%s", title);
    snprintf(r->text, sizeof r->text, "%s", text);
    r->done = done;
    r->arg = arg;
    r->hover = r->pressed = -1;
}

/* JA at panel + (26, 184), NEE at panel + (216, 184), 164x23 plates */
static int req_hit(int x, int y)
{
    for (int b = 0; b < 2; b++) {
        int bx = REQ_X + (b ? 0xd8 : 0x1a), by = REQ_Y + 0xb8;
        if (x >= bx && x < bx + 164 && y >= by && y < by + 23)
            return b;
    }
    return -1;
}

/* print_text_in_box (0x40d130): words wrapped to `w` pixels, '\n' breaks. */
static void wrap_text(m_canvas *c, const m_font *f, int x, int y, int w, const char *s)
{
    int per_line = w / f->w;
    char line[128];
    while (*s) {
        int n = 0, last_space = -1;
        while (s[n] && s[n] != '\n' && n < per_line) {
            if (s[n] == ' ')
                last_space = n;
            n++;
        }
        int cut = n;
        if (s[n] && s[n] != '\n' && s[n] != ' ' && last_space > 0)
            cut = last_space;
        memcpy(line, s, (size_t)cut);
        line[cut] = '\0';
        m_text(c, f, x, y, line);
        s += cut;
        while (*s == ' ')
            s++;
        if (*s == '\n')
            s++;
        y += f->h;
    }
}

static void req_draw(menu *m, m_canvas *c)
{
    m_request *r = &m->req;
    /* 0x411ff0: every pixel with (x + y) odd goes black */
    for (int y = 0; y < M_H; y++)
        for (int x = (y & 1) ^ 1; x < M_W; x += 2)
            c->px[y * M_W + x] = 0;
    /* the panel is drawn by its anchor (200, 111) at the screen centre, so it
     * lands at x 120 -- one right of the (640 - 401) / 2 the buttons and text
     * are laid out from (measured: sprite 150 at (120, 129)) */
    m_sprite p;
    if (m_sprite_get(&m->a, m_anim_frame(m, 56, 0), &p) == 0)
        m_blit(c, &p, M_W / 2 - p.ax, M_H / 2 - p.ay);
    for (int b = 0; b < 2; b++) /* the plates, 151 / lit 152 */
        m_draw_sprite(m, c, r->hover == b ? 152 : 151, REQ_X + (b ? 0xd8 : 0x1a), REQ_Y + 0xb8);
    m_text_centred(c, &m->a.font[M_FONT_STD], REQ_X + 11, REQ_X + 0x187, REQ_Y + 13, r->title);
    wrap_text(c, &m->a.font[M_FONT_MID], 0xa5, 0xaa, 0x140, r->text);
    const m_font *sb = &m->a.font[M_FONT_SMALLBT];
    m_text_centred(c, sb, REQ_X + 0x1a, REQ_X + 0x1a + 164, 317, "JA");
    m_text_centred(c, sb, REQ_X + 0xd8, REQ_X + 0xd8 + 164, 317, "NEE");
}

static void req_answer(menu *m, int yes)
{
    m->req.active = 0;
    if (m->req.done)
        m->req.done(m, yes);
}

/* create_sine_table (0x40bd90): t from 0 in steps of 1.57 / n while t < 1.57,
 * out[i] = (int)(sin(t) * amplitude), truncated toward zero. */
void m_sfx(menu *m, const char *name)
{
    m_audio_sfx(m->audio, name, m->opt.sfx_on ? m->opt.sfx_vol : 0);
}

void m_music_update(menu *m)
{
    static const char *tracks[7] = {"speed.wav", "energy.wav", "jammin.wav", "moody.wav",
                                    "disco.wav", "speed2.wav", "moody.wav"}; /* 0x42ef10 */
    int e = m_slot_int(&m->career, M_SLOT_ETAPPE) % 7;
    if (e < 0)
        e = 0;
    m_audio_music(m->audio, tracks[e], m->opt.music_on ? m->opt.music_vol : 0);
}

static int atext_range(const m_atext *t) { return t->to > t->from ? t->to - t->from : t->from - t->to; }

void m_atext_set(menu *m, int k, const char *text, int from, int to)
{
    m_atext *t = &m->atext[k];
    if (strcmp(t->text, text) == 0 && t->from == from && t->to == to)
        return;
    snprintf(t->text, sizeof t->text, "%s", text);
    t->from = from;
    t->to = to;
    t->step = 0;
    t->last = m->now;
}

static void atext_tick(menu *m, m_atext *t)
{
    int len = (int)strlen(t->text);
    if (!t->text[0] || t->step >= len + atext_range(t) || m->now - t->last <= 33)
        return;
    t->last = m->now;
    if (t->step < len && t->text[t->step] != ' ')
        m_sfx(m, "print.wav"); /* 0x40c1c5 */
    t->step++;
}

/* 0x40c020: characters before the tail in `from`, the tail ramping away */
void m_atext_draw(menu *m, m_canvas *c, int k, const m_font *f, int x, int y)
{
    const m_atext *t = &m->atext[k];
    int len = (int)strlen(t->text), range = atext_range(t);
    int shown = t->step < len ? t->step : len;
    char one[2] = {0, 0};
    for (int i = 0; i < shown; i++) {
        int age = t->step - 1 - i; /* 0 for the newest character */
        int ink = t->from;
        if (age < range)
            ink = t->to > t->from ? t->to - age : t->to + age;
        one[0] = t->text[i];
        m_text_ink(c, f, ink, (x & ~1) + i * f->w, y, one);
    }
}

static void help_play(menu *m);
static void help_draw(menu *m, m_canvas *c);

static int sine_step(int i, int n, int amplitude)
{
    return (int)(sin(i * (1.57 / n)) * amplitude);
}

static const m_form *form(const menu *m) { return &m->d.form[m_screens[m->screen].form]; }

static int is_big(const m_control *c) { return c->type == 1 && c->style == STYLE_BIG; }

static void control_size(const menu *m, const m_control *c, int *w, int *h)
{
    if (c->type == 4) {
        *w = c->w;
        *h = c->h;
        return;
    }
    /* a button is as big as its sprite: anim 4 for HELP, otherwise anim = id */
    m_sprite s;
    if (m_sprite_get(&m->a, m_anim_frame(m, c->id == 4 ? 4 : c->id, 0), &s) == 0) {
        *w = s.w;
        *h = s.h;
    } else {
        *w = 148;
        *h = 42;
    }
}

void m_ui_enter(menu *m, int screen)
{
    const m_screen *s = &m_screens[screen];
    if (m->bg && m->focus >= 0)
        m->kept_focus[m->screen] = m->focus + 1;
    m->prev_screen = m->screen;
    m->screen = screen;
    free(m->bg);
    m->bg = m_background(&m->a, s->bg);
    const m_form *f = form(m);
    m->focus = m->pressed = m->hover = -1;
    int k = 0;
    for (int i = 0; i < f->count; i++) {
        m_slide *sl = &m->slide[i];
        memset(sl, 0, sizeof *sl);
        sl->last = m->now;
        if (is_big(&f->c[i])) {
            sl->delay = s->delay[k++];
        } else {
            sl->step = 8;
        }
        if (f->c[i].state == 1 && f->c[i].type == 1)
            m->focus = i;
        m->disabled[i] = 0;
    }
    if (m->kept_focus[screen] > 0 && m->kept_focus[screen] <= f->count)
        m->focus = m->kept_focus[screen] - 1;
    m->header_step = 0;
    m->header_last = m->now;
    m->help_step = -1;
    if (k)
        m_sfx(m, "slide.wav"); /* add_animated_button, 0x40c461 */
    if (s->enter)
        s->enter(m);
}

void m_ui_tick(menu *m)
{
    const m_form *f = form(m);
    for (int i = 0; i < f->count; i++) {
        m_slide *sl = &m->slide[i];
        if (sl->step >= 8 || m->now - sl->last <= 33)
            continue;
        sl->last = m->now;
        if (sl->delay > 0)
            sl->delay--;
        else
            sl->step++;
    }
    if (m->header_step < 10 && m->now - m->header_last >= 20) {
        m->header_last = m->now;
        m->header_step++;
    }
    if (m->help_step >= 0 && (int32_t)(m->now - m->help_until) >= 0) {
        m->help_step++;
        help_play(m);
    }
    atext_tick(m, &m->atext[0]);
    atext_tick(m, &m->atext[1]);
    if (m->now - m->anim_last > 33) {
        m->anim_last = m->now;
        m->car_frame++;
    }
}

void m_ui_finish(menu *m)
{
    for (int i = 0; i < M_MAX_CONTROLS; i++)
        m->slide[i].step = 8, m->slide[i].delay = 0;
    m->header_step = 10;
    if (m->show_slide >= 0) {
        m->show_cand = m->show_slide_to;
        m->show_slide = -1;
    }
    for (int k = 0; k < 2; k++)
        m->atext[k].step = (int)strlen(m->atext[k].text) + atext_range(&m->atext[k]);
}

static void draw_button(menu *m, m_canvas *c, int i)
{
    const m_control *ct = &form(m)->c[i];
    int x = ct->x, y = ct->y;
    const m_slide *sl = &m->slide[i];
    if (is_big(ct) && sl->step < 8) {
        x = -100 + sine_step(sl->step, 8, ct->x + 100);
        y = ct->y;
    }
    if (ct->id == 4) {
        m_draw_sprite(m, c, m_anim_frame(m, 4, m->hover == i ? 1 : 0), x, y);
        return;
    }
    /* frames: 0 normal, 1 pressed, 2 disabled (blank on the big buttons),
     * 3 focused.  add_animated_button and the small buttons take their anim
     * from the button id (garage 7/8/9, showroom arrows 23/24, KOPEN 25) */
    int state = 0;
    if (m->disabled[i])
        state = 2;
    else if (m->pressed == i && m->hover == i)
        state = 1;
    else if (m->focus == i)
        state = 3;
    m_sprite s;
    int fr = m_anim_frame(m, ct->id, state);
    if (fr < 0 || m_sprite_get(&m->a, fr, &s) != 0)
        return;
    m_blit(c, &s, x, y);
    const char *label = ct->label;
    if (m->screen == SCR_MAIN && ct->id == 5 && m->career.finished_career)
        label = "Restart"; /* 0x40703a */
    if (!label[0])
        return;
    if (is_big(ct)) { /* BUTFNT (capitals only), centred over x .. x + 148 at y + 8 */
        char up[32];
        size_t n = 0;
        for (; label[n] && n < sizeof up - 1; n++)
            up[n] = (char)toupper((unsigned char)label[n]);
        up[n] = '\0';
        m_text_centred(c, &m->a.font[M_FONT_BUT], x, x + s.w, y + 8, up);
    } else if (m->disabled[i]) {
        /* a disabled small button's label is grey Windows text (it matches no
         * menu font): stand-in */
        const m_font *f = &m->a.font[M_FONT_STD];
        int tx = x + ((s.w + 1) - m_text_width(f, label)) / 2;
        m_text_ink(c, f, 116, tx, y + (s.h - 17) / 2 + 1, label);
    } else { /* SMALLBT as written, one lower than centred (KOPEN 437, JA 317) */
        m_text_centred(c, &m->a.font[M_FONT_SMALLBT], x, x + s.w, y + (s.h - 17) / 2 + 1, label);
    }
}

void m_ui_draw(menu *m, m_canvas *c, int cursor)
{
    const m_screen *s = &m_screens[m->screen];
    m_fill(c, m->bg);
    if (s->draw)
        s->draw(m, c);
    int hy = m->header_step >= 10 ? 0 : sine_step(m->header_step, 10, 47) - 47;
    m_draw_sprite(m, c, m_anim_frame(m, 3, s->header), 0, hy);
    const m_form *f = form(m);
    for (int i = 0; i < f->count; i++)
        if (f->c[i].type == 1)
            draw_button(m, c, i);
    if (s->overlay)
        s->overlay(m, c);
    if (m->help_step >= 0)
        help_draw(m, c);
    if (m->req.active)
        req_draw(m, c);
    if (cursor)
        m_draw_sprite(m, c, m_anim_frame(m, 2, 0), m->mouse_x, m->mouse_y);
}

static int hit(const menu *m, int x, int y)
{
    const m_form *f = form(m);
    for (int i = 0; i < f->count; i++) {
        const m_control *ct = &f->c[i];
        if (ct->type != 1 || m->slide[i].step < 8 || m->disabled[i])
            continue;
        int w, h;
        control_size(m, ct, &w, &h);
        if (x >= ct->x && x < ct->x + w && y >= ct->y && y < ct->y + h)
            return i;
    }
    return -1;
}

/* ---- HELP (play_help 0x40fcb0): the screen darkened with the checkerboard
 * except the control being explained, its clip playing; a key or a click
 * ends the tour ---------------------------------------------------------- */

static void help_play(menu *m)
{
    const m_help *h = m_screens[m->screen].help;
    if (!h || h[m->help_step].control < 0) {
        m->help_step = -1;
        return;
    }
    m_sfx(m, h[m->help_step].wav);
    int ms = m_audio_clip_ms(m->audio, h[m->help_step].wav);
    m->help_until = m->now + (uint32_t)(ms > 0 ? ms : 1500);
}

static void help_start(menu *m)
{
    m->help_step = 0;
    help_play(m);
}

static void help_stop(menu *m)
{
    m->help_step = -1;
    m_audio_stop_sfx(m->audio);
}

static void help_draw(menu *m, m_canvas *c)
{
    const m_help *h = m_screens[m->screen].help;
    const m_control *ct = &form(m)->c[h[m->help_step].control];
    int w, hh;
    control_size(m, ct, &w, &hh);
    for (int y = 0; y < M_H; y++)
        for (int x = (y & 1) ^ 1; x < M_W; x += 2)
            if (x < ct->x || x >= ct->x + w || y < ct->y || y >= ct->y + hh)
                c->px[y * M_W + x] = 0;
}

static void activate(menu *m, int i)
{
    const m_control *ct = &form(m)->c[i];
    if (ct->id == 4 && m_screens[m->screen].help) { /* the HELP badge */
        m->focus = i;
        m_sfx(m, "klik.wav");
        help_start(m);
        return;
    }
    m->focus = i;
    m_sfx(m, "klik.wav");
    if (m_screens[m->screen].action)
        m_screens[m->screen].action(m, ct->id);
}

void m_ui_mouse_move(menu *m, int x, int y)
{
    m->mouse_x = x;
    m->mouse_y = y;
    if (m->req.active) {
        m->req.hover = req_hit(x, y);
        m->hover = -1;
        return;
    }
    m->hover = hit(m, x, y);
    if (m_screens[m->screen].hover)
        m_screens[m->screen].hover(m, x, y);
}

/* menu.dat acts on the mouse press, not the release: holding the button down
 * on GARAGE already shows the garage (Wine capture), and the screens' event
 * handlers play klik.wav as they take the event -- so the click is heard as
 * the button goes down. */
void m_ui_mouse_button(menu *m, int down)
{
    if (m->show_slide >= 0)
        return; /* the original's slide is a blocking loop */
    if (!down) {
        m->pressed = -1;
        if (m->req.active)
            m->req.pressed = -1;
        return;
    }
    if (m->help_step >= 0) {
        help_stop(m);
        return;
    }
    if (m->req.active) {
        int b = req_hit(m->mouse_x, m->mouse_y);
        if (b >= 0) {
            m->req.pressed = b;
            m_sfx(m, "klik.wav");
            req_answer(m, b == 0);
        }
        return;
    }
    int i = hit(m, m->mouse_x, m->mouse_y);
    if (i >= 0) {
        m->pressed = i; /* shown pressed while the button is held */
        activate(m, i);
        return;
    }
    if (m_screens[m->screen].click && m_screens[m->screen].click(m, m->mouse_x, m->mouse_y))
        m_sfx(m, "klik.wav");
}

void m_ui_key(menu *m, int sym)
{
    if (m->show_slide >= 0)
        return;
    const m_form *f = form(m);
    if (m->help_step >= 0) {
        help_stop(m);
        return;
    }
    if (!m->req.active && m_screens[m->screen].key && m_screens[m->screen].key(m, sym))
        return;
    if (m->req.active) {
        if (sym == SDLK_j || sym == SDLK_RETURN)
            req_answer(m, 1);
        else if (sym == SDLK_n || sym == SDLK_ESCAPE)
            req_answer(m, 0); /* 0x2710, escape, is "no" (0x4025dc) */
        return;
    }
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
        if (m->focus >= 0)
            activate(m, m->focus);
        return;
    }
    if (sym == SDLK_UP || sym == SDLK_DOWN) {
        int dir = sym == SDLK_DOWN ? 1 : -1;
        for (int k = 1; k <= f->count; k++) {
            int i = ((m->focus < 0 ? 0 : m->focus) + dir * k + f->count * 2) % f->count;
            if (is_big(&f->c[i])) {
                m->focus = i;
                m_sfx(m, "click3.wav");
                break;
            }
        }
        return;
    }
    if (sym >= 0 && sym < 128 && isalnum(sym)) {
        for (int i = 0; i < f->count; i++)
            if (f->c[i].type == 1 && f->c[i].id != 4 &&
                toupper((unsigned char)f->c[i].hotkey) == toupper(sym)) {
                activate(m, i);
                return;
            }
    }
}
