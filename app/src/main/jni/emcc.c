/*
 * emcc.c: the C component of an Emscripten-based web/Javascript front
 * end for Puzzles.
 *
 * The Javascript parts of this system live in emcclib.js and
 * emccpre.js. It also depends on being run in the context of a web
 * page containing an appropriate collection of bits and pieces (a
 * canvas, some buttons and links etc), which is generated for each
 * puzzle by the script html/jspage.pl.
 */

/*
 * Further thoughts on possible enhancements:
 *
 *  - I should think about whether these webified puzzles can support
 *    touchscreen-based tablet browsers.
 *
 *  - think about making use of localStorage. It might be useful to
 *    let the user save games into there as an alternative to disk
 *    files - disk files are all very well for getting the save right
 *    out of your browser to (e.g.) email to me as a bug report, but
 *    for just resuming a game you were in the middle of, you'd
 *    probably rather have a nice simple 'quick save' and 'quick load'
 *    button pair.
 *
 *  - this is a downright silly idea, but it does occur to me that if
 *    I were to write a PDF output driver for the Puzzles printing
 *    API, then I might be able to implement a sort of 'printing'
 *    feature in this front end, using data: URIs again. (Ask the user
 *    exactly what they want printed, then construct an appropriate
 *    PDF and embed it in a gigantic data: URI. Then they can print
 *    that using whatever they normally use to print PDFs!)
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif

#include "puzzles.h"

/*
 * Extern references to Javascript functions provided in emcclib.js.
 */
extern void js_init_puzzle(void);
extern void js_post_init(void);
extern void js_debug(const char *);
extern void js_error_box(const char *message);
extern void js_remove_type_dropdown(void);
extern void js_remove_solve_button(void);
extern void js_add_preset(int menuid, const char *name, int value);
extern int js_add_preset_submenu(int menuid, const char *name);
extern int js_get_selected_preset(void);
extern void js_select_preset(int n);
extern void js_default_colour(float *output);
extern void js_set_colour(int colour_number, const char *colour_string);
extern void js_get_date_64(unsigned *p);
extern void js_update_permalinks(const char *desc, const char *seed);
extern void js_enable_undo_redo(bool undo, bool redo);
extern void js_update_key_labels(const char *lsk, const char *csk);
extern void js_activate_timer(void);
extern void js_deactivate_timer(void);
extern void js_canvas_start_draw(drawing *dr);
extern void js_canvas_draw_update(drawing *dr, int x, int y, int w, int h);
extern void js_canvas_end_draw(drawing *dr);
extern void js_canvas_draw_rect(drawing *dr,
                                int x, int y, int w, int h, int colour);
extern void js_canvas_clip(drawing *dr, int x, int y, int w, int h);
extern void js_canvas_unclip(drawing *dr);
extern void js_canvas_draw_line(float x1, float y1, float x2, float y2,
                                int width, int colour);
extern void js_canvas_draw_poly(drawing *dr, const int *points, int npoints,
                                int fillcolour, int outlinecolour);
extern void js_canvas_draw_circle(drawing *dr, int x, int y, int r,
                                  int fillcolour, int outlinecolour);
extern int js_canvas_find_font_midpoint(int height, bool monospaced);
extern void js_canvas_draw_text(int x, int y, int halign,
                                int colour, int height,
                                bool monospaced, const char *text);
extern void js_canvas_new_blitter(blitter *bl, int w, int h);
extern void js_canvas_free_blitter(blitter *bl);
extern void js_canvas_blitter_save(drawing *dr, blitter *bl, int x, int y);
extern void js_canvas_blitter_load(drawing *dr, blitter *bl, int x, int y);
extern void js_canvas_remove_statusbar(void);
extern void js_canvas_status_bar(drawing *dr, const char *text);
extern bool js_canvas_get_preferred_size(int *wp, int *hp);
extern void js_canvas_set_size(int w, int h, int fe_scale);
extern double js_get_device_pixel_ratio(void);

extern void js_dialog_init(const char *title);
extern void js_dialog_string(int i, const char *title, const char *initvalue);
extern void js_dialog_choices(int i, const char *title, const char *choicelist,
                              int initvalue);
extern void js_dialog_boolean(int i, const char *title, bool initvalue);
extern void js_dialog_launch(void);
extern void js_dialog_cleanup(void);
extern void js_focus_canvas(void);

extern bool js_savefile_read(void *buf, int len);

extern void js_save_prefs(const char *);
extern void js_load_prefs(midend *);

/*
 * These functions are called from JavaScript, so their prototypes
 * need to be kept in sync with emccpre.js.
 */
bool mouseup(int x, int y, int button);
bool mousedown(int x, int y, int button);
bool mousemove(int x, int y, int buttons);
bool key(int keycode, const char *key, const char *chr, int location,
         bool shift, bool ctrl);
void timer_callback(double tplus);
void command(int n);
char *get_text_format(void);
void free_save_file(char *buffer);
char *get_save_file(void);
void free_save_file(char *buffer);
void load_game(void);
void dlg_return_sval(int index, const char *val);
void dlg_return_ival(int index, int val);
void resize_puzzle(int w, int h);
void restore_puzzle_size(int w, int h);
void rescale_puzzle(void);

/*
 * Internal forward references.
 */
static void save_prefs(midend *me);

/*
 * Call JS to get the date, and use that to initialise our random
 * number generator to invent the first game seed.
 */
void get_random_seed(void **randseed, int *randseedsize)
{
    unsigned *ret = snewn(2, unsigned);
    js_get_date_64(ret);
    *randseed = ret;
    *randseedsize = 2*sizeof(unsigned);
}

/*
 * Fatal error, called in cases of complete despair such as when
 * malloc() has returned NULL.
 */
void fatal(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    strcpy(buf, "puzzle fatal error: ");

    va_start(ap, fmt);
    vsnprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
    va_end(ap);

    js_error_box(buf);
}

#ifdef DEBUGGING
void debug_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    js_debug(buf);
}
#endif

/*
 * Helper function that makes it easy to test strings that might be
 * NULL.
 */
static int strnullcmp(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return a != NULL ? +1 : b != NULL ? -1 : 0;
    return strcmp(a, b);
}

/*
 * The global midend object.
 */
static midend *me;

/* ----------------------------------------------------------------------
 * Timing functions.
 */
static bool timer_active = false;
void deactivate_timer(frontend *fe)
{
    js_deactivate_timer();
    timer_active = false;
}
void activate_timer(frontend *fe)
{
    if (!timer_active) {
        js_activate_timer();
        timer_active = true;
    }
}
void timer_callback(double tplus)
{
    if (timer_active)
        midend_timer(me, tplus);
}

/* ----------------------------------------------------------------------
 * Helper functions to resize the canvas, and variables to remember
 * its size for other functions.
 */
static int canvas_w, canvas_h;

/*
 * Called when we resize as a result of changing puzzle settings
 * or device pixel ratio.
 *
 * fe_scale is an integer that will be used to scale all drawing
 * operations to the canvas.  This means that at large device pixel
 * ratios, thin lines get a bit thicker so that they're still visible
 * on high-density screens.  But the device pixel ratio might not be
 * an integer, so we keep the remaining fractional scale and tell the
 * mid end about that so that it can use it to adjust the tile size
 * and achieve the correct overall scaling.
 */
static void resize(void)
{
    int w, h, fe_scale = 1;
    bool user;
    double dpr;

    w = h = INT_MAX;
    user = js_canvas_get_preferred_size(&w, &h);
    dpr = js_get_device_pixel_ratio();
    if (dpr >= 2) {
        fe_scale = floor(dpr);
        dpr /= fe_scale;
        w /= fe_scale;
        h /= fe_scale;
    }
    midend_size(me, &w, &h, user, dpr);
    js_canvas_set_size(w * fe_scale, h * fe_scale, fe_scale);
    canvas_w = w * fe_scale;
    canvas_h = h * fe_scale;
}

/* Called from JS when the device pixel ratio changes */
void rescale_puzzle(void)
{
    resize();
    midend_force_redraw(me);
}

/* Called from JS when the user uses the resize handle */
void resize_puzzle(int w, int h)
{
    int fe_scale = 1;
    double dpr;

    dpr = js_get_device_pixel_ratio();
    if (dpr >= 2) {
        fe_scale = floor(dpr);
        dpr /= fe_scale;
        w /= fe_scale;
        h /= fe_scale;
    }
    midend_size(me, &w, &h, true, dpr);
    if (canvas_w != w || canvas_h != h) { 
        js_canvas_set_size(w * fe_scale, h * fe_scale, fe_scale);
        canvas_w = w * fe_scale;
        canvas_h = h * fe_scale;
        midend_force_redraw(me);
    }
}

/* Called from JS when the user uses the restore button */
void restore_puzzle_size(int w, int h)
{
    midend_reset_tilesize(me);
    resize();
    midend_force_redraw(me);
}

/*
 * Try to extract a background colour from the canvas's CSS.  In case
 * it doesn't have a usable one, make up a lightish grey ourselves.
 */
void frontend_default_colour(frontend *fe, float *output)
{
    output[0] = output[1] = output[2] = 0.9F;
    js_default_colour(output);
}

/*
 * Helper function called from all over the place to ensure the undo
 * and redo buttons get properly enabled and disabled after every move
 * or undo or new-game event.
 */
static void post_move(void)
{
    js_enable_undo_redo(midend_can_undo(me), midend_can_redo(me));
    js_update_key_labels(midend_current_key_label(me, CURSOR_SELECT2),
                         midend_current_key_label(me, CURSOR_SELECT));
}

/*
 * Mouse event handlers called from JS.
 */
bool mousedown(int x, int y, int button)
{
    bool handled;

    button = (button == 0 ? LEFT_BUTTON :
              button == 1 ? MIDDLE_BUTTON : RIGHT_BUTTON);
    handled = midend_process_key(me, x, y, button) != PKR_UNUSED;
    post_move();
    return handled;
}

bool mouseup(int x, int y, int button)
{
    bool handled;

    button = (button == 0 ? LEFT_RELEASE :
              button == 1 ? MIDDLE_RELEASE : RIGHT_RELEASE);
    handled = midend_process_key(me, x, y, button) != PKR_UNUSED;
    post_move();
    return handled;
}

bool mousemove(int x, int y, int buttons)
{
    int button = (buttons & 2 ? MIDDLE_DRAG :
                  buttons & 4 ? RIGHT_DRAG : LEFT_DRAG);
    bool handled;

    handled = midend_process_key(me, x, y, button) != PKR_UNUSED;
    post_move();
    return handled;
}

/*
 * Keyboard handler called from JS.  Returns true if the key was
 * handled and hence the keydown event should be cancelled.
 */
bool key(int keycode, const char *key, const char *chr, int location,
         bool shift, bool ctrl)
{
    /* Key location constants from JavaScript. */
    #define DOM_KEY_LOCATION_STANDARD 0
    #define DOM_KEY_LOCATION_LEFT     1
    #define DOM_KEY_LOCATION_RIGHT    2
    #define DOM_KEY_LOCATION_NUMPAD   3
    int keyevent = -1;
    int process_key_result;

    if (!strnullcmp(key, "Backspace") || !strnullcmp(key, "Delete") ||
        !strnullcmp(key, "Del"))
        keyevent = 127;                /* Backspace / Delete */
    else if (!strnullcmp(key, "Enter"))
        keyevent = 13;             /* return */
    else if (!strnullcmp(key, "Spacebar"))
        keyevent = ' ';
    else if (!strnullcmp(key, "Escape"))
        keyevent = 27;
    else if (!strnullcmp(key, "ArrowLeft") || !strnullcmp(key, "Left"))
        keyevent = CURSOR_LEFT;
    else if (!strnullcmp(key, "ArrowUp") || !strnullcmp(key, "Up"))
        keyevent = CURSOR_UP;
    else if (!strnullcmp(key, "ArrowRight") || !strnullcmp(key, "Right"))
        keyevent = CURSOR_RIGHT;
    else if (!strnullcmp(key, "ArrowDown") || !strnullcmp(key, "Down"))
        keyevent = CURSOR_DOWN;
    else if (!strnullcmp(key, "SoftLeft"))
        /* Left soft key on KaiOS. */
        keyevent = CURSOR_SELECT2;
    else if (!strnullcmp(key, "End"))
        /*
         * We interpret Home, End, PgUp and PgDn as numeric keypad
         * controls regardless of whether they're the ones on the
         * numeric keypad (since we can't tell). The effect of
         * this should only be that the non-numeric-pad versions
         * of those keys generate directions in 8-way movement
         * puzzles like Cube and Inertia.
         */
        keyevent = MOD_NUM_KEYPAD | '1';
    else if (!strnullcmp(key, "PageDown"))
        keyevent = MOD_NUM_KEYPAD | '3';
    else if (!strnullcmp(key, "Home"))
        keyevent = MOD_NUM_KEYPAD | '7';
    else if (!strnullcmp(key, "PageUp"))
        keyevent = MOD_NUM_KEYPAD | '9';
    else if (shift && ctrl && (!strnullcmp(key, "Z") || !strnullcmp(key, "z")))
        keyevent = UI_REDO;
    else if (key && (unsigned char)key[0] < 0x80 && key[1] == '\0')
        /* Key generating a single ASCII character. */
        keyevent = key[0];
    /*
     * In modern browsers (since about 2017), all keys that Puzzles
     * cares about should be matched by one of the clauses above.  The
     * code below that checks keycode and chr should be relavent only
     * in older browsers.
     */
    else if (keycode == 8 || keycode == 46)
        keyevent = 127;                /* Backspace / Delete */
    else if (keycode == 13)
        keyevent = 13;             /* return */
    else if (keycode == 37)
        keyevent = CURSOR_LEFT;
    else if (keycode == 38)
        keyevent = CURSOR_UP;
    else if (keycode == 39)
        keyevent = CURSOR_RIGHT;
    else if (keycode == 40)
        keyevent = CURSOR_DOWN;
    else if (keycode == 35)
        keyevent = MOD_NUM_KEYPAD | '1';
    else if (keycode == 34)
        keyevent = MOD_NUM_KEYPAD | '3';
    else if (keycode == 36)
        keyevent = MOD_NUM_KEYPAD | '7';
    else if (keycode == 33)
        keyevent = MOD_NUM_KEYPAD | '9';
    else if (shift && ctrl && (keycode & 0x1F) == 26)
        keyevent = UI_REDO;
    else if (chr && chr[0] && !chr[1])
        keyevent = chr[0] & 0xFF;
    else if (keycode >= 96 && keycode < 106)
        keyevent = MOD_NUM_KEYPAD | ('0' + keycode - 96);
    else if (keycode >= 65 && keycode <= 90)
        keyevent = keycode + (shift ? 0 : 32);
    else if (keycode >= 48 && keycode <= 57)
        keyevent = keycode;
    else if (keycode == 32)        /* space / CURSOR_SELECT2 */
        keyevent = keycode;

    if (keyevent >= 0) {
        if (shift) keyevent |= MOD_SHFT;
        if (ctrl) keyevent |= MOD_CTRL;
        if (location == DOM_KEY_LOCATION_NUMPAD) keyevent |= MOD_NUM_KEYPAD;

        process_key_result = midend_process_key(me, 0, 0, keyevent);
        post_move();
        /*
         * Treat Backspace specially because that's expected on KaiOS.
         * https://developer.kaiostech.com/docs/design-guide/key
         */
        if (process_key_result == PKR_NO_EFFECT &&
            !strnullcmp(key, "Backspace"))
            return false;
        return process_key_result != PKR_UNUSED;
    }
    return false; /* Event not handled, because we don't even recognise it. */
}

/*
 * Helper function called from several places to update the permalinks
 * whenever a new game is created.
 */
static void update_permalinks(void)
{
    char *desc, *seed;
    desc = midend_get_game_id(me);
    seed = midend_get_random_seed(me);
    js_update_permalinks(desc, seed);
    sfree(desc);
    sfree(seed);
}

/*
 * Callback from the midend when the game ids change, so we can update
 * the permalinks.
 */
static void ids_changed(void *ignored)
{
    update_permalinks();
}

/* ----------------------------------------------------------------------
 * Wrappers for some drawing API functions.  The rest of the implementation
 * is in emcclib.js.
 */
static void js_draw_text(drawing *dr, int x, int y, int fonttype,
                         int fontsize, int align, int colour,
                         const char *text)
{
    int halign;

    if (align & ALIGN_VCENTRE)
	y += js_canvas_find_font_midpoint(fontsize, fonttype == FONT_FIXED);

    if (align & ALIGN_HCENTRE)
	halign = 1;
    else if (align & ALIGN_HRIGHT)
        halign = 2;
    else
        halign = 0;

    js_canvas_draw_text(x, y, halign, colour,
                        fontsize, fonttype == FONT_FIXED, text);
}

static void js_draw_line(drawing *dr, int x1, int y1, int x2, int y2,
                         int colour)
{
    js_canvas_draw_line(x1, y1, x2, y2, 1, colour);
}

static void js_draw_thick_line(drawing *dr, float thickness,
                               float x1, float y1, float x2, float y2,
                               int colour)
{
    js_canvas_draw_line(x1, y1, x2, y2, thickness, colour);
}

struct blitter {
    char dummy;
};

static blitter *js_blitter_new(drawing *dr, int w, int h)
{
    blitter *bl = snew(blitter);
    js_canvas_new_blitter(bl, w, h);
    return bl;
}

static void js_blitter_free(drawing *dr, blitter *bl)
{
    js_canvas_free_blitter(bl);
    sfree(bl);
}

static char *js_text_fallback(drawing *dr, const char *const *strings,
                              int nstrings)
{
    return dupstr(strings[0]); /* Emscripten has no trouble with UTF-8 */
}

static const struct drawing_api js_drawing = {
    1,
    js_draw_text,
    js_canvas_draw_rect,
    js_draw_line,
#ifdef USE_DRAW_POLYGON_FALLBACK
    draw_polygon_fallback,
#else
    js_canvas_draw_poly,
#endif
    js_canvas_draw_circle,
    js_canvas_draw_update,
    js_canvas_clip,
    js_canvas_unclip,
    js_canvas_start_draw,
    js_canvas_end_draw,
    js_canvas_status_bar,
    js_blitter_new,
    js_blitter_free,
    js_canvas_blitter_save,
    js_canvas_blitter_load,
    NULL, NULL, NULL, NULL, NULL, NULL, /* {begin,end}_{doc,page,puzzle} */
    NULL, NULL,			       /* line_width, line_dotted */
    js_text_fallback,
    js_draw_thick_line,
};

/* ----------------------------------------------------------------------
 * Presets and game-configuration dialog support.
 */
static game_params **presets;
static int npresets;
static bool have_presets_dropdown;

static void populate_js_preset_menu(int menuid, struct preset_menu *menu)
{
    int i;
    for (i = 0; i < menu->n_entries; i++) {
        struct preset_menu_entry *entry = &menu->entries[i];
        if (entry->params) {
            presets[entry->id] = entry->params;
            js_add_preset(menuid, entry->title, entry->id);
        } else {
            int js_submenu = js_add_preset_submenu(menuid, entry->title);
            populate_js_preset_menu(js_submenu, entry->submenu);
        }
    }
}

static void select_appropriate_preset(void)
{
    if (have_presets_dropdown) {
        int preset = midend_which_preset(me);
        js_select_preset(preset < 0 ? -1 : preset);
    }
}

static config_item *cfg = NULL;
static int cfg_which;

/*
 * Set up a dialog box. This is pretty easy on the C side; most of the
 * work is done in JS.
 */
static void cfg_start(int which)
{
    char *title;
    int i;

    cfg = midend_get_config(me, which, &title);
    cfg_which = which;

    js_dialog_init(title);
    sfree(title);

    for (i = 0; cfg[i].type != C_END; i++) {
	switch (cfg[i].type) {
	  case C_STRING:
            js_dialog_string(i, cfg[i].name, cfg[i].u.string.sval);
	    break;
	  case C_BOOLEAN:
            js_dialog_boolean(i, cfg[i].name, cfg[i].u.boolean.bval);
	    break;
	  case C_CHOICES:
            js_dialog_choices(i, cfg[i].name, cfg[i].u.choices.choicenames,
                              cfg[i].u.choices.selected);
	    break;
	}
    }

    js_dialog_launch();
}

/*
 * Callbacks from JS when the OK button is clicked, to return the
 * final state of each control.
 */
void dlg_return_sval(int index, const char *val)
{
    config_item *i = cfg + index;
    switch (i->type) {
      case C_STRING:
        sfree(i->u.string.sval);
        i->u.string.sval = dupstr(val);
        break;
      default:
        assert(0 && "Bad type for return_sval");
    }
}
void dlg_return_ival(int index, int val)
{
    config_item *i = cfg + index;
    switch (i->type) {
      case C_BOOLEAN:
        i->u.boolean.bval = val;
        break;
      case C_CHOICES:
        i->u.choices.selected = val;
        break;
      default:
        assert(0 && "Bad type for return_ival");
    }
}

/*
 * Called when the user clicks OK or Cancel. use_results will be true
 * or false respectively, in those cases. We terminate the dialog box,
 * unless the user selected an invalid combination of parameters.
 */
static void cfg_end(bool use_results)
{
    if (use_results) {
        /*
         * User hit OK.
         */
        const char *err = midend_set_config(me, cfg_which, cfg);

        if (err) {
            /*
             * The settings were unacceptable, so leave the config box
             * open for the user to adjust them and try again.
             */
            js_error_box(err);
        } else if (cfg_which == CFG_PREFS) {
            /*
             * Acceptable settings for user preferences: enact them
             * without blowing away the current game.
             */
            resize();
            midend_redraw(me);
            free_cfg(cfg);
            js_dialog_cleanup();
            save_prefs(me);
        } else {
            /*
             * Acceptable settings for the remaining configuration
             * types: start a new game and close the dialog.
             */
            select_appropriate_preset();
            midend_new_game(me);
            resize();
            midend_redraw(me);
            free_cfg(cfg);
            js_dialog_cleanup();
        }
    } else {
        /*
         * User hit Cancel. Close the dialog, but also we must still
         * reselect the right element of the dropdown list.
         *
         * (Because: imagine you have a preset selected, and then you
         * select Custom from the list, but change your mind and hit
         * Esc. The Custom option will now still be selected in the
         * list, whereas obviously it should show the preset you still
         * _actually_ have selected.)
         */
        select_appropriate_preset();

        free_cfg(cfg);
        js_dialog_cleanup();
    }
}

/* ----------------------------------------------------------------------
 * Called from JS when a command is given to the puzzle by clicking a
 * button or control of some sort.
 */
void command(int n)
{
    switch (n) {
      case 0:                          /* specific game ID */
        cfg_start(CFG_DESC);
        break;
      case 1:                          /* random game seed */
        cfg_start(CFG_SEED);
        break;
      case 2:                          /* game parameter dropdown changed */
        {
            int i = js_get_selected_preset();
            if (i < 0) {
                /*
                 * The user selected 'Custom', so launch the config
                 * box.
                 */
                if (thegame.can_configure) /* (double-check just in case) */
                    cfg_start(CFG_SETTINGS);
            } else {
                /*
                 * The user selected a preset, so just switch straight
                 * to that.
                 */
                assert(i < npresets);
                midend_set_params(me, presets[i]);
                midend_new_game(me);
                resize();
                midend_redraw(me);
                post_move();
                js_focus_canvas();
                select_appropriate_preset();
            }
        }
        break;
      case 3:                          /* OK clicked in a config box */
        cfg_end(true);
        post_move();
        break;
      case 4:                          /* Cancel clicked in a config box */
        cfg_end(false);
        post_move();
        break;
      case 5:                          /* New Game */
        midend_process_key(me, 0, 0, UI_NEWGAME);
        post_move();
        js_focus_canvas();
        break;
      case 6:                          /* Restart */
        midend_restart_game(me);
        post_move();
        js_focus_canvas();
        break;
      case 7:                          /* Undo */
        midend_process_key(me, 0, 0, UI_UNDO);
        post_move();
        js_focus_canvas();
        break;
      case 8:                          /* Redo */
        midend_process_key(me, 0, 0, UI_REDO);
        post_move();
        js_focus_canvas();
        break;
      case 9:                          /* Solve */
        if (thegame.can_solve) {
            const char *msg = midend_solve(me);
            if (msg)
                js_error_box(msg);
        }
        post_move();
        js_focus_canvas();
        break;
      case 10:                         /* user preferences */
        cfg_start(CFG_PREFS);
        break;
    }
}

char *get_text_format(void)
{
    return midend_text_format(me);
}

void free_text_format(char *buffer)
{
    sfree(buffer);
}

/* ----------------------------------------------------------------------
 * Called from JS to prepare a save-game file, and free one after it's
 * been used.
 */

struct savefile_write_ctx {
    char *buffer;
    size_t pos;
};

static void savefile_write(void *vctx, const void *buf, int len)
{
    struct savefile_write_ctx *ctx = (struct savefile_write_ctx *)vctx;
    if (ctx->buffer)
        memcpy(ctx->buffer + ctx->pos, buf, len);
    ctx->pos += len;
}

char *get_save_file(void)
{
    struct savefile_write_ctx ctx;
    size_t size;

    /* First pass, to count up the size */
    ctx.buffer = NULL;
    ctx.pos = 0;
    midend_serialise(me, savefile_write, &ctx);
    size = ctx.pos;

    /* Second pass, to actually write out the data. We have to put a
     * terminating \0 on the end (which we expect never to show up in
     * the actual serialisation format - it's text, not binary) so
     * that the Javascript side can easily find out the length. */
    ctx.buffer = snewn(size+1, char);
    ctx.pos = 0;
    midend_serialise(me, savefile_write, &ctx);
    assert(ctx.pos == size);
    ctx.buffer[ctx.pos] = '\0';

    return ctx.buffer;
}

void free_save_file(char *buffer)
{
    sfree(buffer);
}

static bool savefile_read(void *vctx, void *buf, int len)
{
    return js_savefile_read(buf, len);
}

void load_game(void)
{
    const char *err;

    /*
     * savefile_read_callback in JavaScript was set up by our caller
     * as a closure that knows what file we're loading.
     */
    err = midend_deserialise(me, savefile_read, NULL);

    if (err) {
        js_error_box(err);
    } else {
        select_appropriate_preset();
        resize();
        midend_redraw(me);
        update_permalinks();
        post_move();
    }
}

/* ----------------------------------------------------------------------
 * Functions to load and save preferences, calling out to JS to access
 * the appropriate localStorage slot.
 */

static void save_prefs(midend *me)
{
    struct savefile_write_ctx ctx;
    size_t size;

    /* First pass, to count up the size */
    ctx.buffer = NULL;
    ctx.pos = 0;
    midend_save_prefs(me, savefile_write, &ctx);
    size = ctx.pos;

    /* Second pass, to actually write out the data. As with
     * get_save_file, we append a terminating \0. */
    ctx.buffer = snewn(size+1, char);
    ctx.pos = 0;
    midend_save_prefs(me, savefile_write, &ctx);
    assert(ctx.pos == size);
    ctx.buffer[ctx.pos] = '\0';

    js_save_prefs(ctx.buffer);

    sfree(ctx.buffer);
}

struct prefs_read_ctx {
    const char *buffer;
    size_t pos, len;
};

static bool prefs_read(void *vctx, void *buf, int len)
{
    struct prefs_read_ctx *ctx = (struct prefs_read_ctx *)vctx;

    if (len < 0)
        return false;
    if (ctx->len - ctx->pos < len)
        return false;
    memcpy(buf, ctx->buffer + ctx->pos, len);
    ctx->pos += len;
    return true;
}

void prefs_load_callback(midend *me, const char *prefs)
{
    struct prefs_read_ctx ctx;

    ctx.buffer = prefs;
    ctx.len = strlen(prefs);
    ctx.pos = 0;

    midend_load_prefs(me, prefs_read, &ctx);
}

/* ----------------------------------------------------------------------
 * Setup function called at page load time. It's called main() because
 * that's the most convenient thing in Emscripten, but it's not main()
 * in the usual sense of bounding the program's entire execution.
 * Instead, this function returns once the initial puzzle is set up
 * and working, and everything thereafter happens by means of JS event
 * handlers sending us callbacks.
 */
int main(int argc, char **argv)
{
    const char *param_err;
    float *colours;
    int i, ncolours;

    /*
     * Initialise JavaScript event handlers.
     */
    js_init_puzzle();

    /*
     * Instantiate a midend.
     */
    me = midend_new(NULL, &thegame, &js_drawing, NULL);
    js_load_prefs(me);

    /*
     * Chuck in the HTML fragment ID if we have one (trimming the
     * leading # off the front first). If that's invalid, we retain
     * the error message and will display it at the end, after setting
     * up a random puzzle as usual.
     */
    if (argc > 1 && argv[1][0] == '#' && argv[1][1] != '\0')
        param_err = midend_game_id(me, argv[1] + 1);
    else
        param_err = NULL;

    /*
     * Create either a random game or the specified one, and set the
     * canvas size appropriately.
     */
    midend_new_game(me);
    resize();

    /*
     * Remove the status bar, if not needed.
     */
    if (!midend_wants_statusbar(me))
        js_canvas_remove_statusbar();

    /*
     * Set up the game-type dropdown with presets and/or the Custom
     * option.
     */
    {
        struct preset_menu *menu = midend_get_presets(me, &npresets);
        bool may_configure = false;
        presets = snewn(npresets, game_params *);
        for (i = 0; i < npresets; i++)
            presets[i] = NULL;

        populate_js_preset_menu(0, menu);

        /*
         * Crude hack to allow the "Custom..." item to be hidden on
         * KaiOS, where dialogs don't yet work.
         */
        if (thegame.can_configure && getenv_bool("PUZZLES_ALLOW_CUSTOM", true))
            may_configure = true;
        if (may_configure)
            js_add_preset(0, "Custom...", -1);

        have_presets_dropdown = npresets > 1 || may_configure;

        if (have_presets_dropdown)
            /*
             * Now ensure the appropriate element of the presets menu
             * starts off selected, in case it isn't the first one in the
             * list (e.g. Slant).
             */
            select_appropriate_preset();
        else
            js_remove_type_dropdown();
    }

    /*
     * Remove the Solve button if the game doesn't support it.
     */
    if (!thegame.can_solve)
        js_remove_solve_button();

    /*
     * Retrieve the game's colours, and convert them into #abcdef type
     * hex ID strings.
     */
    colours = midend_colours(me, &ncolours);
    for (i = 0; i < ncolours; i++) {
        char col[40];
        sprintf(col, "#%02x%02x%02x",
                (unsigned)(0.5F + 255 * colours[i*3+0]),
                (unsigned)(0.5F + 255 * colours[i*3+1]),
                (unsigned)(0.5F + 255 * colours[i*3+2]));
        js_set_colour(i, col);
    }

    /*
     * Request notification when the game ids change (e.g. if the user
     * presses 'n', and also when Mines supersedes its game
     * description), so that we can proactively update the permalink.
     */
    midend_request_id_changes(me, ids_changed, NULL);

    /*
     * Draw the puzzle's initial state, and set up the permalinks and
     * undo/redo greying out.
     */
    midend_redraw(me);
    update_permalinks();
    post_move();

    /*
     * If we were given an erroneous game ID in argv[1], now's the
     * time to put up the error box about it, after we've fully set up
     * a random puzzle. Then when the user clicks 'ok', we have a
     * puzzle for them.
     */
    if (param_err)
        js_error_box(param_err);

    /*
     * Reveal the puzzle!
     */
    js_post_init();

    /*
     * Done. Return to JS, and await callbacks!
     */
    return 0;
}
