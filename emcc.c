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
 *  - I think it might be feasible to have these JS puzzles permit
 *    loading and saving games in disk files. Saving would be done by
 *    constructing a data: URI encapsulating the save file, and then
 *    telling the browser to visit that URI with the effect that it
 *    would naturally pop up a 'where would you like to save this'
 *    dialog box. Loading, more or less similarly, might be feasible
 *    by using the DOM File API to ask the user to select a file and
 *    permit us to see its contents.
 *
 *  - I should think about whether these webified puzzles can support
 *    touchscreen-based tablet browsers (assuming there are any that
 *    can cope with the reasonably modern JS and run it fast enough to
 *    be worthwhile).
 *
 *  - think about making use of localStorage. It might be useful to
 *    let the user save games into there as an alternative to disk
 *    files - disk files are all very well for getting the save right
 *    out of your browser to (e.g.) email to me as a bug report, but
 *    for just resuming a game you were in the middle of, you'd
 *    probably rather have a nice simple 'quick save' and 'quick load'
 *    button pair. Also, that might be a useful place to store
 *    preferences, if I ever get round to writing a preferences UI.
 *
 *  - some CSS to make the button bar and configuration dialogs a
 *    little less ugly would probably not go amiss.
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

#include "puzzles.h"

/*
 * Extern references to Javascript functions provided in emcclib.js.
 */
extern void js_debug(const char *);
extern void js_error_box(const char *message);
extern void js_remove_type_dropdown(void);
extern void js_remove_solve_button(void);
extern void js_add_preset(const char *name);
extern int js_get_selected_preset(void);
extern void js_select_preset(int n);
extern void js_get_date_64(unsigned *p);
extern void js_update_permalinks(const char *desc, const char *seed);
extern void js_enable_undo_redo(int undo, int redo);
extern void js_activate_timer();
extern void js_deactivate_timer();
extern void js_canvas_start_draw(void);
extern void js_canvas_draw_update(int x, int y, int w, int h);
extern void js_canvas_end_draw(void);
extern void js_canvas_draw_rect(int x, int y, int w, int h,
                                const char *colour);
extern void js_canvas_clip_rect(int x, int y, int w, int h);
extern void js_canvas_unclip(void);
extern void js_canvas_draw_line(float x1, float y1, float x2, float y2,
                                int width, const char *colour);
extern void js_canvas_draw_poly(int *points, int npoints,
                                const char *fillcolour,
                                const char *outlinecolour);
extern void js_canvas_draw_circle(int x, int y, int r,
                                  const char *fillcolour,
                                  const char *outlinecolour);
extern int js_canvas_find_font_midpoint(int height, const char *fontptr);
extern void js_canvas_draw_text(int x, int y, int halign,
                                const char *colptr, const char *fontptr,
                                const char *text);
extern int js_canvas_new_blitter(int w, int h);
extern void js_canvas_free_blitter(int id);
extern void js_canvas_copy_to_blitter(int id, int x, int y, int w, int h);
extern void js_canvas_copy_from_blitter(int id, int x, int y, int w, int h);
extern void js_canvas_make_statusbar(void);
extern void js_canvas_set_statusbar(const char *text);
extern void js_canvas_set_size(int w, int h);

extern void js_dialog_init(const char *title);
extern void js_dialog_string(int i, const char *title, const char *initvalue);
extern void js_dialog_choices(int i, const char *title, const char *choicelist,
                              int initvalue);
extern void js_dialog_boolean(int i, const char *title, int initvalue);
extern void js_dialog_launch(void);
extern void js_dialog_cleanup(void);
extern void js_focus_canvas(void);

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
void fatal(char *fmt, ...)
{
    char buf[512];
    va_list ap;

    strcpy(buf, "puzzle fatal error: ");

    va_start(ap, fmt);
    vsnprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
    va_end(ap);

    js_error_box(buf);
}

void debug_printf(char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    js_debug(buf);
}

/*
 * Helper function that makes it easy to test strings that might be
 * NULL.
 */
int strnullcmp(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return a != NULL ? +1 : b != NULL ? -1 : 0;
    return strcmp(a, b);
}

/*
 * HTMLish names for the colours allocated by the puzzle.
 */
char **colour_strings;
int ncolours;

/*
 * The global midend object.
 */
midend *me;

/* ----------------------------------------------------------------------
 * Timing functions.
 */
int timer_active = FALSE;
void deactivate_timer(frontend *fe)
{
    js_deactivate_timer();
    timer_active = FALSE;
}
void activate_timer(frontend *fe)
{
    if (!timer_active) {
        js_activate_timer();
        timer_active = TRUE;
    }
}
void timer_callback(double tplus)
{
    if (timer_active)
        midend_timer(me, tplus);
}

/* ----------------------------------------------------------------------
 * Helper functions to resize the canvas, and variables to remember
 * its size for other functions (e.g. trimming blitter rectangles).
 */
static int canvas_w, canvas_h;

/* Called when we resize as a result of changing puzzle settings */
static void resize(void)
{
    int w, h;
    w = h = INT_MAX;
    midend_size(me, &w, &h, FALSE);
    js_canvas_set_size(w, h);
    canvas_w = w;
    canvas_h = h;
}

/* Called from JS when the user uses the resize handle */
void resize_puzzle(int w, int h)
{
    midend_size(me, &w, &h, TRUE);
    if (canvas_w != w || canvas_h != h) { 
        js_canvas_set_size(w, h);
        canvas_w = w;
        canvas_h = h;
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
 * HTML doesn't give us a default frontend colour of its own, so we
 * just make up a lightish grey ourselves.
 */
void frontend_default_colour(frontend *fe, float *output)
{
    output[0] = output[1] = output[2] = 0.9F;
}

/*
 * Helper function called from all over the place to ensure the undo
 * and redo buttons get properly enabled and disabled after every move
 * or undo or new-game event.
 */
static void update_undo_redo(void)
{
    js_enable_undo_redo(midend_can_undo(me), midend_can_redo(me));
}

/*
 * Mouse event handlers called from JS.
 */
void mousedown(int x, int y, int button)
{
    button = (button == 0 ? LEFT_BUTTON :
              button == 1 ? MIDDLE_BUTTON : RIGHT_BUTTON);
    midend_process_key(me, x, y, button);
    update_undo_redo();
}

void mouseup(int x, int y, int button)
{
    button = (button == 0 ? LEFT_RELEASE :
              button == 1 ? MIDDLE_RELEASE : RIGHT_RELEASE);
    midend_process_key(me, x, y, button);
    update_undo_redo();
}

void mousemove(int x, int y, int buttons)
{
    int button = (buttons & 2 ? MIDDLE_DRAG :
                  buttons & 4 ? RIGHT_DRAG : LEFT_DRAG);
    midend_process_key(me, x, y, button);
    update_undo_redo();
}

/*
 * Keyboard handler called from JS.
 */
void key(int keycode, int charcode, const char *key, const char *chr,
         int shift, int ctrl)
{
    int keyevent = -1;

    if (!strnullcmp(key, "Backspace") || !strnullcmp(key, "Del") ||
        keycode == 8 || keycode == 46) {
        keyevent = 127;                /* Backspace / Delete */
    } else if (!strnullcmp(key, "Enter") || keycode == 13) {
        keyevent = 13;             /* return */
    } else if (!strnullcmp(key, "Left") || keycode == 37) {
        keyevent = CURSOR_LEFT;
    } else if (!strnullcmp(key, "Up") || keycode == 38) {
        keyevent = CURSOR_UP;
    } else if (!strnullcmp(key, "Right") || keycode == 39) {
        keyevent = CURSOR_RIGHT;
    } else if (!strnullcmp(key, "Down") || keycode == 40) {
        keyevent = CURSOR_DOWN;
    } else if (!strnullcmp(key, "End") || keycode == 35) {
        /*
         * We interpret Home, End, PgUp and PgDn as numeric keypad
         * controls regardless of whether they're the ones on the
         * numeric keypad (since we can't tell). The effect of
         * this should only be that the non-numeric-pad versions
         * of those keys generate directions in 8-way movement
         * puzzles like Cube and Inertia.
         */
        keyevent = MOD_NUM_KEYPAD | '1';
    } else if (!strnullcmp(key, "PageDown") || keycode==34) {
        keyevent = MOD_NUM_KEYPAD | '3';
    } else if (!strnullcmp(key, "Home") || keycode==36) {
        keyevent = MOD_NUM_KEYPAD | '7';
    } else if (!strnullcmp(key, "PageUp") || keycode==33) {
        keyevent = MOD_NUM_KEYPAD | '9';
    } else if (chr && chr[0] && !chr[1]) {
        keyevent = chr[0] & 0xFF;
    } else if (keycode >= 96 && keycode < 106) {
        keyevent = MOD_NUM_KEYPAD | ('0' + keycode - 96);
    } else if (keycode >= 65 && keycode <= 90) {
        keyevent = keycode + (shift ? 0 : 32);
    } else if (keycode >= 48 && keycode <= 57) {
        keyevent = keycode;
    } else if (keycode == 32) {        /* space / CURSOR_SELECT2 */
        keyevent = keycode;
    }

    if (keyevent >= 0) {
        if (shift && keyevent >= 0x100)
            keyevent |= MOD_SHFT;

        if (ctrl) {
            if (keyevent >= 0x100)
                keyevent |= MOD_CTRL;
            else
                keyevent &= 0x1F;
        }

        midend_process_key(me, 0, 0, keyevent);
        update_undo_redo();
    }
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
 * Implementation of the drawing API by calling Javascript canvas
 * drawing functions. (Well, half of it; the other half is on the JS
 * side.)
 */
static void js_start_draw(void *handle)
{
    js_canvas_start_draw();
}

static void js_clip(void *handle, int x, int y, int w, int h)
{
    js_canvas_clip_rect(x, y, w, h);
}

static void js_unclip(void *handle)
{
    js_canvas_unclip();
}

static void js_draw_text(void *handle, int x, int y, int fonttype,
                         int fontsize, int align, int colour, char *text)
{
    char fontstyle[80];
    int halign;

    sprintf(fontstyle, "%dpx %s", fontsize,
            fonttype == FONT_FIXED ? "monospace" : "sans-serif");

    if (align & ALIGN_VCENTRE)
	y += js_canvas_find_font_midpoint(fontsize, fontstyle);

    if (align & ALIGN_HCENTRE)
	halign = 1;
    else if (align & ALIGN_HRIGHT)
        halign = 2;
    else
        halign = 0;

    js_canvas_draw_text(x, y, halign, colour_strings[colour], fontstyle, text);
}

static void js_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
    js_canvas_draw_rect(x, y, w, h, colour_strings[colour]);
}

static void js_draw_line(void *handle, int x1, int y1, int x2, int y2,
                         int colour)
{
    js_canvas_draw_line(x1, y1, x2, y2, 1, colour_strings[colour]);
}

static void js_draw_thick_line(void *handle, float thickness,
                               float x1, float y1, float x2, float y2,
                               int colour)
{
    js_canvas_draw_line(x1, y1, x2, y2, thickness, colour_strings[colour]);
}

static void js_draw_poly(void *handle, int *coords, int npoints,
                         int fillcolour, int outlinecolour)
{
    js_canvas_draw_poly(coords, npoints,
                        fillcolour >= 0 ? colour_strings[fillcolour] : NULL,
                        colour_strings[outlinecolour]);
}

static void js_draw_circle(void *handle, int cx, int cy, int radius,
                           int fillcolour, int outlinecolour)
{
    js_canvas_draw_circle(cx, cy, radius,
                          fillcolour >= 0 ? colour_strings[fillcolour] : NULL,
                          colour_strings[outlinecolour]);
}

struct blitter {
    int id;                            /* allocated on the js side */
    int w, h;                          /* easier to retain here */
};

static blitter *js_blitter_new(void *handle, int w, int h)
{
    blitter *bl = snew(blitter);
    bl->w = w;
    bl->h = h;
    bl->id = js_canvas_new_blitter(w, h);
    return bl;
}

static void js_blitter_free(void *handle, blitter *bl)
{
    js_canvas_free_blitter(bl->id);
    sfree(bl);
}

static void trim_rect(int *x, int *y, int *w, int *h)
{
    int x0, x1, y0, y1;

    /*
     * Reduce the size of the copied rectangle to stop it going
     * outside the bounds of the canvas.
     */

    /* Transform from x,y,w,h form into coordinates of all edges */
    x0 = *x;
    y0 = *y;
    x1 = *x + *w;
    y1 = *y + *h;

    /* Clip each coordinate at both extremes of the canvas */
    x0 = (x0 < 0 ? 0 : x0 > canvas_w ? canvas_w : x0);
    x1 = (x1 < 0 ? 0 : x1 > canvas_w ? canvas_w : x1);
    y0 = (y0 < 0 ? 0 : y0 > canvas_h ? canvas_h : y0);
    y1 = (y1 < 0 ? 0 : y1 > canvas_h ? canvas_h : y1); 

    /* Transform back into x,y,w,h to return */
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
}

static void js_blitter_save(void *handle, blitter *bl, int x, int y)
{
    int w = bl->w, h = bl->h;
    trim_rect(&x, &y, &w, &h);
    if (w > 0 && h > 0)
        js_canvas_copy_to_blitter(bl->id, x, y, w, h);
}

static void js_blitter_load(void *handle, blitter *bl, int x, int y)
{
    int w = bl->w, h = bl->h;
    trim_rect(&x, &y, &w, &h);
    if (w > 0 && h > 0)
        js_canvas_copy_from_blitter(bl->id, x, y, w, h);
}

static void js_draw_update(void *handle, int x, int y, int w, int h)
{
    trim_rect(&x, &y, &w, &h);
    if (w > 0 && h > 0)
        js_canvas_draw_update(x, y, w, h);
}

static void js_end_draw(void *handle)
{
    js_canvas_end_draw();
}

static void js_status_bar(void *handle, char *text)
{
    js_canvas_set_statusbar(text);
}

static char *js_text_fallback(void *handle, const char *const *strings,
                              int nstrings)
{
    return dupstr(strings[0]); /* Emscripten has no trouble with UTF-8 */
}

const struct drawing_api js_drawing = {
    js_draw_text,
    js_draw_rect,
    js_draw_line,
    js_draw_poly,
    js_draw_circle,
    js_draw_update,
    js_clip,
    js_unclip,
    js_start_draw,
    js_end_draw,
    js_status_bar,
    js_blitter_new,
    js_blitter_free,
    js_blitter_save,
    js_blitter_load,
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
int have_presets_dropdown;

void select_appropriate_preset(void)
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
            js_dialog_string(i, cfg[i].name, cfg[i].sval);
	    break;
	  case C_BOOLEAN:
            js_dialog_boolean(i, cfg[i].name, cfg[i].ival);
	    break;
	  case C_CHOICES:
            js_dialog_choices(i, cfg[i].name, cfg[i].sval, cfg[i].ival);
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
    sfree(cfg[index].sval);
    cfg[index].sval = dupstr(val);
}
void dlg_return_ival(int index, int val)
{
    cfg[index].ival = val;
}

/*
 * Called when the user clicks OK or Cancel. use_results will be TRUE
 * or FALSE respectively, in those cases. We terminate the dialog box,
 * unless the user selected an invalid combination of parameters.
 */
static void cfg_end(int use_results)
{
    if (use_results) {
        /*
         * User hit OK.
         */
        char *err = midend_set_config(me, cfg_which, cfg);

        if (err) {
            /*
             * The settings were unacceptable, so leave the config box
             * open for the user to adjust them and try again.
             */
            js_error_box(err);
        } else {
            /*
             * New settings are fine; start a new game and close the
             * dialog.
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
         * _actually_ have selected. Worse still, it'll be the visible
         * rather than invisible Custom option - see the comment in
         * js_add_preset in emcclib.js - so you won't even be able to
         * select Custom without a faffy workaround.)
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
                update_undo_redo();
                js_focus_canvas();
                select_appropriate_preset(); /* sort out Custom/Customise */
            }
        }
        break;
      case 3:                          /* OK clicked in a config box */
        cfg_end(TRUE);
        update_undo_redo();
        break;
      case 4:                          /* Cancel clicked in a config box */
        cfg_end(FALSE);
        update_undo_redo();
        break;
      case 5:                          /* New Game */
        midend_process_key(me, 0, 0, 'n');
        update_undo_redo();
        js_focus_canvas();
        break;
      case 6:                          /* Restart */
        midend_restart_game(me);
        update_undo_redo();
        js_focus_canvas();
        break;
      case 7:                          /* Undo */
        midend_process_key(me, 0, 0, 'u');
        update_undo_redo();
        js_focus_canvas();
        break;
      case 8:                          /* Redo */
        midend_process_key(me, 0, 0, 'r');
        update_undo_redo();
        js_focus_canvas();
        break;
      case 9:                          /* Solve */
        if (thegame.can_solve) {
            char *msg = midend_solve(me);
            if (msg)
                js_error_box(msg);
        }
        update_undo_redo();
        js_focus_canvas();
        break;
    }
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
    char *param_err;
    float *colours;
    int i;

    /*
     * Instantiate a midend.
     */
    me = midend_new(NULL, &thegame, &js_drawing, NULL);

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
     * Create a status bar, if needed.
     */
    if (midend_wants_statusbar(me))
        js_canvas_make_statusbar();

    /*
     * Set up the game-type dropdown with presets and/or the Custom
     * option.
     */
    npresets = midend_num_presets(me);
    if (npresets == 0) {
        /*
         * This puzzle doesn't have selectable game types at all.
         * Completely remove the drop-down list from the page.
         */
        js_remove_type_dropdown();
        have_presets_dropdown = FALSE;
    } else {
        presets = snewn(npresets, game_params *);
        for (i = 0; i < npresets; i++) {
            char *name;
            midend_fetch_preset(me, i, &name, &presets[i]);
            js_add_preset(name);
        }
        if (thegame.can_configure)
            js_add_preset(NULL);   /* the 'Custom' entry in the dropdown */

        have_presets_dropdown = TRUE;

        /*
         * Now ensure the appropriate element of the presets menu
         * starts off selected, in case it isn't the first one in the
         * list (e.g. Slant).
         */
        select_appropriate_preset();
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
    colour_strings = snewn(ncolours, char *);
    for (i = 0; i < ncolours; i++) {
        char col[40];
        sprintf(col, "#%02x%02x%02x",
                (unsigned)(0.5 + 255 * colours[i*3+0]),
                (unsigned)(0.5 + 255 * colours[i*3+1]),
                (unsigned)(0.5 + 255 * colours[i*3+2]));
        colour_strings[i] = dupstr(col);
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
    update_undo_redo();

    /*
     * If we were given an erroneous game ID in argv[1], now's the
     * time to put up the error box about it, after we've fully set up
     * a random puzzle. Then when the user clicks 'ok', we have a
     * puzzle for them.
     */
    if (param_err)
        js_error_box(param_err);

    /*
     * Done. Return to JS, and await callbacks!
     */
    return 0;
}
