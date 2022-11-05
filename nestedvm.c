/*
 * nestedvm.c: NestedVM front end for my puzzle collection.
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include <sys/time.h>

#include "puzzles.h"

extern void _pause();
extern int _call_java(int cmd, int arg1, int arg2, int arg3);

void fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "fatal error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

struct frontend {
    // TODO kill unneeded members!
    midend *me;
    bool timer_active;
    struct timeval last_time;
    config_item *cfg;
    int cfg_which;
    bool cfgret;
    int ox, oy, w, h;
};

static frontend *_fe;

void get_random_seed(void **randseed, int *randseedsize)
{
    struct timeval *tvp = snew(struct timeval);
    gettimeofday(tvp, NULL);
    *randseed = (void *)tvp;
    *randseedsize = sizeof(struct timeval);
}

void frontend_default_colour(frontend *fe, float *output)
{
    output[0] = output[1]= output[2] = 0.8f;
}

void nestedvm_status_bar(void *handle, const char *text)
{
    _call_java(4,0,(int)text,0);
}

void nestedvm_start_draw(void *handle)
{
    frontend *fe = (frontend *)handle;
    _call_java(5, 0, fe->w, fe->h);
    _call_java(4, 1, fe->ox, fe->oy);
}

void nestedvm_clip(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    _call_java(5, w, h, 0);
    _call_java(4, 3, x + fe->ox, y + fe->oy);
}

void nestedvm_unclip(void *handle)
{
    frontend *fe = (frontend *)handle;
    _call_java(4, 4, fe->ox, fe->oy);
}

void nestedvm_draw_text(void *handle, int x, int y, int fonttype, int fontsize,
                        int align, int colour, const char *text)
{
    frontend *fe = (frontend *)handle;
    _call_java(5, x + fe->ox, y + fe->oy, 
	       (fonttype == FONT_FIXED ? 0x10 : 0x0) | align);
    _call_java(7, fontsize, colour, (int)text);
}

void nestedvm_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
    frontend *fe = (frontend *)handle;
    _call_java(5, w, h, colour);
    _call_java(4, 5, x + fe->ox, y + fe->oy);
}

void nestedvm_draw_line(void *handle, int x1, int y1, int x2, int y2, 
			int colour)
{
    frontend *fe = (frontend *)handle;
    _call_java(5, x2 + fe->ox, y2 + fe->oy, colour);
    _call_java(4, 6, x1 + fe->ox, y1 + fe->oy);
}

void nestedvm_draw_poly(void *handle, int *coords, int npoints,
			int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    int i;
    _call_java(4, 7, npoints, 0);
    for (i = 0; i < npoints; i++) {
	_call_java(6, i, coords[i*2] + fe->ox, coords[i*2+1] + fe->oy);
    }
    _call_java(4, 8, outlinecolour, fillcolour);
}

void nestedvm_draw_circle(void *handle, int cx, int cy, int radius,
		     int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    _call_java(5, cx+fe->ox, cy+fe->oy, radius);
    _call_java(4, 9, outlinecolour, fillcolour);
}

struct blitter {
    int handle, w, h, x, y;
};

blitter *nestedvm_blitter_new(void *handle, int w, int h)
{
    blitter *bl = snew(blitter);
    bl->handle = -1;
    bl->w = w;
    bl->h = h;
    return bl;
}

void nestedvm_blitter_free(void *handle, blitter *bl)
{
    if (bl->handle != -1)
	_call_java(4, 11, bl->handle, 0);
    sfree(bl);
}

void nestedvm_blitter_save(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;    
    if (bl->handle == -1)
	bl->handle = _call_java(4,10,bl->w, bl->h);
    bl->x = x;
    bl->y = y;
    _call_java(8, bl->handle, x + fe->ox, y + fe->oy);
}

void nestedvm_blitter_load(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    assert(bl->handle != -1);
    if (x == BLITTER_FROMSAVED && y == BLITTER_FROMSAVED) {
        x = bl->x;
        y = bl->y;
    }
    _call_java(9, bl->handle, x + fe->ox, y + fe->oy);
}

void nestedvm_end_draw(void *handle)
{
    _call_java(4,2,0,0);
}

char *nestedvm_text_fallback(void *handle, const char *const *strings,
			     int nstrings)
{
    /*
     * We assume Java can cope with any UTF-8 likely to be emitted
     * by a puzzle.
     */
    return dupstr(strings[0]);
}

const struct drawing_api nestedvm_drawing = {
    nestedvm_draw_text,
    nestedvm_draw_rect,
    nestedvm_draw_line,
    nestedvm_draw_poly,
    nestedvm_draw_circle,
    NULL, // draw_update,
    nestedvm_clip,
    nestedvm_unclip,
    nestedvm_start_draw,
    nestedvm_end_draw,
    nestedvm_status_bar,
    nestedvm_blitter_new,
    nestedvm_blitter_free,
    nestedvm_blitter_save,
    nestedvm_blitter_load,
    NULL, NULL, NULL, NULL, NULL, NULL, /* {begin,end}_{doc,page,puzzle} */
    NULL, NULL,			       /* line_width, line_dotted */
    nestedvm_text_fallback,
};

int jcallback_key_event(int x, int y, int keyval)
{
    frontend *fe = (frontend *)_fe;
    if (fe->ox == -1)
        return 1;
    if (keyval >= 0 &&
        !midend_process_key(fe->me, x - fe->ox, y - fe->oy, keyval, NULL))
	return 42;
    return 1;
}

int jcallback_resize(int width, int height)
{
    frontend *fe = (frontend *)_fe;
    int x, y;
    x = width;
    y = height;
    midend_size(fe->me, &x, &y, true, 1.0);
    fe->ox = (width - x) / 2;
    fe->oy = (height - y) / 2;
    fe->w = x;
    fe->h = y;
    midend_force_redraw(fe->me);
    return 0;
}

int jcallback_timer_func()
{
    frontend *fe = (frontend *)_fe;
    if (fe->timer_active) {
	struct timeval now;
	float elapsed;
	gettimeofday(&now, NULL);
	elapsed = ((now.tv_usec - fe->last_time.tv_usec) * 0.000001F +
		   (now.tv_sec - fe->last_time.tv_sec));
        midend_timer(fe->me, elapsed);	/* may clear timer_active */
	fe->last_time = now;
    }
    return fe->timer_active;
}

void deactivate_timer(frontend *fe)
{
    if (fe->timer_active)
	_call_java(4, 13, 0, 0);
    fe->timer_active = false;
}

void activate_timer(frontend *fe)
{
    if (!fe->timer_active) {
	_call_java(4, 12, 0, 0);
	gettimeofday(&fe->last_time, NULL);
    }
    fe->timer_active = true;
}

void jcallback_config_ok()
{
    frontend *fe = (frontend *)_fe;
    const char *err;

    err = midend_set_config(fe->me, fe->cfg_which, fe->cfg);

    if (err)
	_call_java(2, (int) "Error", (int)err, 1);
    else {
	fe->cfgret = true;
    }
}

void jcallback_config_set_string(int item_ptr, int char_ptr) {
    config_item *i = (config_item *)item_ptr;
    char* newval = (char*) char_ptr;
    assert(i->type == C_STRING);
    sfree(i->u.string.sval);
    i->u.string.sval = dupstr(newval);
    free(newval);
}

void jcallback_config_set_boolean(int item_ptr, int selected) {
    config_item *i = (config_item *)item_ptr;
    assert(i->type == C_BOOLEAN);
    i->u.boolean.bval = selected != 0 ? true : false;
}

void jcallback_config_set_choice(int item_ptr, int selected) {
    config_item *i = (config_item *)item_ptr;
    assert(i->type == C_CHOICES);
    i->u.choices.selected = selected;
}

static bool get_config(frontend *fe, int which)
{
    char *title;
    config_item *i;
    fe->cfg = midend_get_config(fe->me, which, &title);
    fe->cfg_which = which;
    fe->cfgret = false;
    _call_java(10, (int)title, 0, 0);
    for (i = fe->cfg; i->type != C_END; i++) {
	_call_java(5, (int)i, i->type, (int)i->name);
        switch (i->type) {
          case C_STRING:
            _call_java(11, (int)i->u.string.sval, 0, 0);
            break;
          case C_BOOLEAN:
            _call_java(11, 0, i->u.boolean.bval, 0);
            break;
          case C_CHOICES:
            _call_java(11, (int)i->u.choices.choicenames,
                       i->u.choices.selected, 0);
            break;
        }
    }
    _call_java(12,0,0,0);
    free_cfg(fe->cfg);
    return fe->cfgret;
}

int jcallback_newgame_event(void)
{
    frontend *fe = (frontend *)_fe;
    if (!midend_process_key(fe->me, 0, 0, UI_NEWGAME, NULL))
	return 42;
    return 0;
}

int jcallback_undo_event(void)
{
    frontend *fe = (frontend *)_fe;
    if (!midend_process_key(fe->me, 0, 0, UI_UNDO, NULL))
	return 42;
    return 0;
}

int jcallback_redo_event(void)
{
    frontend *fe = (frontend *)_fe;
    if (!midend_process_key(fe->me, 0, 0, UI_REDO, NULL))
	return 42;
    return 0;
}

int jcallback_quit_event(void)
{
    frontend *fe = (frontend *)_fe;
    if (!midend_process_key(fe->me, 0, 0, UI_QUIT, NULL))
	return 42;
    return 0;
}

static void resize_fe(frontend *fe)
{
    int x, y;

    x = INT_MAX;
    y = INT_MAX;
    midend_size(fe->me, &x, &y, false, 1.0);
    _call_java(3, x, y, 0);
}

int jcallback_preset_event(int ptr_game_params)
{
    frontend *fe = (frontend *)_fe;
    game_params *params =
	(game_params *)ptr_game_params;

    midend_set_params(fe->me, params);
    midend_new_game(fe->me);
    resize_fe(fe);
    _call_java(13, midend_which_preset(fe->me), 0, 0);
    return 0;
}

int jcallback_solve_event()
{
    frontend *fe = (frontend *)_fe;
    const char *msg;

    msg = midend_solve(fe->me);

    if (msg)
	_call_java(2, (int) "Error", (int)msg, 1);
    return 0;
}

int jcallback_restart_event()
{
    frontend *fe = (frontend *)_fe;

    midend_restart_game(fe->me);
    return 0;
}

int jcallback_config_event(int which)
{
    frontend *fe = (frontend *)_fe;
    _call_java(13, midend_which_preset(fe->me), 0, 0);
    if (!get_config(fe, which))
	return 0;
    midend_new_game(fe->me);
    resize_fe(fe);
    _call_java(13, midend_which_preset(fe->me), 0, 0);
    return 0;
}

int jcallback_about_event()
{
    char titlebuf[256];
    char textbuf[1024];

    sprintf(titlebuf, "About %.200s", thegame.name);
    sprintf(textbuf,
	    "%.200s\n\n"
	    "from Simon Tatham's Portable Puzzle Collection\n\n"
	    "%.500s", thegame.name, ver);
    _call_java(2, (int)&titlebuf, (int)&textbuf, 0);
    return 0;
}

void preset_menu_populate(struct preset_menu *menu, int menuid)
{
    int i;

    for (i = 0; i < menu->n_entries; i++) {
        struct preset_menu_entry *entry = &menu->entries[i];
        if (entry->params) {
            _call_java(5, (int)entry->params, 0, 0);
            _call_java(1, (int)entry->title, menuid, entry->id);
        } else {
            _call_java(5, 0, 0, 0);
            _call_java(1, (int)entry->title, menuid, entry->id);
            preset_menu_populate(entry->submenu, entry->id);
        }
    }
}

int main(int argc, char **argv)
{
    int i, n;
    float* colours;

    _fe = snew(frontend);
    _fe->timer_active = false;
    _fe->me = midend_new(_fe, &thegame, &nestedvm_drawing, _fe);
    if (argc > 1)
	midend_game_id(_fe->me, argv[1]);   /* ignore failure */
    midend_new_game(_fe->me);

    {
        struct preset_menu *menu;
        int nids, topmenu;
        menu = midend_get_presets(_fe->me, &nids);
        topmenu = _call_java(1, 0, nids, 0);
        preset_menu_populate(menu, topmenu);
    }

    colours = midend_colours(_fe->me, &n);
    _fe->ox = -1;

    _call_java(0, (int)thegame.name,
	       (thegame.can_configure ? 1 : 0) |
	       (midend_wants_statusbar(_fe->me) ? 2 : 0) |
	       (thegame.can_solve ? 4 : 0), n);    
    for (i = 0; i < n; i++) {
	_call_java(1024+ i,
		   (int)(colours[i*3] * 0xFF),
		   (int)(colours[i*3+1] * 0xFF),
		   (int)(colours[i*3+2] * 0xFF));
    }
    resize_fe(_fe);

    _call_java(13, midend_which_preset(_fe->me), 0, 0);

    // Now pause the vm. The VM will be call()ed when
    // an input event occurs.
    _pause();

    // shut down when the VM is resumed.
    deactivate_timer(_fe);
    midend_free(_fe->me);
    return 0;
}
