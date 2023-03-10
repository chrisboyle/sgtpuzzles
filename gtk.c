/*
 * gtk.c: GTK front end for my puzzle collection.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1 /* for strcasestr */
#endif

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/resource.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "puzzles.h"
#include "gtk.h"

#if GTK_CHECK_VERSION(2,0,0)
# define USE_PANGO
# ifdef PANGO_VERSION_CHECK
#  if PANGO_VERSION_CHECK(1,8,0)
#   define HAVE_SENSIBLE_ABSOLUTE_SIZE_FUNCTION
#  endif
# endif
#endif
#if !GTK_CHECK_VERSION(2,4,0)
# define OLD_FILESEL
#endif
#if GTK_CHECK_VERSION(2,8,0)
# define USE_CAIRO
# if GTK_CHECK_VERSION(3,0,0) || defined(GDK_DISABLE_DEPRECATED)
#  define USE_CAIRO_WITHOUT_PIXMAP
# endif
#endif

#if defined USE_CAIRO && GTK_CHECK_VERSION(2,10,0)
/* We can only use printing if we are using Cairo for drawing and we
   have a GTK version >= 2.10 (when GtkPrintOperation was added). */
# define USE_PRINTING
# if GTK_CHECK_VERSION(2,18,0)
/* We can embed the page setup. Before 2.18, we needed to have a
   separate page setup. */
#  define USE_EMBED_PAGE_SETUP
# endif
#endif

#if GTK_CHECK_VERSION(3,0,0)
/* The old names are still more concise! */
#define gtk_hbox_new(x,y) gtk_box_new(GTK_ORIENTATION_HORIZONTAL,y)
#define gtk_vbox_new(x,y) gtk_box_new(GTK_ORIENTATION_VERTICAL,y)
/* GTK 3 has retired stock button labels */
#define LABEL_OK "_OK"
#define LABEL_CANCEL "_Cancel"
#define LABEL_NO "_No"
#define LABEL_YES "_Yes"
#define LABEL_SAVE "_Save"
#define LABEL_OPEN "_Open"
#define gtk_button_new_with_our_label gtk_button_new_with_mnemonic
#else
#define LABEL_OK GTK_STOCK_OK
#define LABEL_CANCEL GTK_STOCK_CANCEL
#define LABEL_NO GTK_STOCK_NO
#define LABEL_YES GTK_STOCK_YES
#define LABEL_SAVE GTK_STOCK_SAVE
#define LABEL_OPEN GTK_STOCK_OPEN
#define gtk_button_new_with_our_label gtk_button_new_from_stock
#endif

/* #undef USE_CAIRO */
/* #define NO_THICK_LINE */
#ifdef DEBUGGING
static FILE *debug_fp = NULL;

static void dputs(const char *buf)
{
    if (!debug_fp) {
        debug_fp = fopen("debug.log", "w");
    }

    fputs(buf, stderr);

    if (debug_fp) {
        fputs(buf, debug_fp);
        fflush(debug_fp);
    }
}

void debug_printf(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    dputs(buf);
    va_end(ap);
}
#endif

/* ----------------------------------------------------------------------
 * Error reporting functions used elsewhere.
 */

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

/* ----------------------------------------------------------------------
 * GTK front end to puzzles.
 */

static void changed_preset(frontend *fe);

struct font {
#ifdef USE_PANGO
    PangoFontDescription *desc;
#else
    GdkFont *font;
#endif
    int type;
    int size;
};

/*
 * An internal API for functions which need to be different for
 * printing and drawing.
 */
struct internal_drawing_api {
    void (*set_colour)(frontend *fe, int colour);
#ifdef USE_CAIRO
    void (*fill)(frontend *fe);
    void (*fill_preserve)(frontend *fe);
#endif
};

/*
 * This structure holds all the data relevant to a single window.
 * In principle this would allow us to open multiple independent
 * puzzle windows, although I can't currently see any real point in
 * doing so. I'm just coding cleanly because there's no
 * particularly good reason not to.
 */
struct frontend {
    bool headless; /* true if we're running without GTK, for --screenshot */

    GtkWidget *window;
    GtkAccelGroup *dummy_accelgroup;
    GtkWidget *area;
    GtkWidget *statusbar;
    GtkWidget *menubar;
#if GTK_CHECK_VERSION(3,20,0)
    GtkCssProvider *css_provider;
#endif
    guint statusctx;
    int w, h;
    midend *me;
#ifdef USE_CAIRO
    const float *colours;
    cairo_t *cr;
    cairo_surface_t *image;
#ifndef USE_CAIRO_WITHOUT_PIXMAP
    GdkPixmap *pixmap;
#endif
    GdkColor background;	       /* for painting outside puzzle area */
#else
    GdkPixmap *pixmap;
    GdkGC *gc;
    GdkColor *colours;
    GdkColormap *colmap;
    int backgroundindex;	       /* which of colours[] is background */
#endif
    int ncolours;
    int bbox_l, bbox_r, bbox_u, bbox_d;
    bool timer_active;
    int timer_id;
    struct timeval last_time;
    struct font *fonts;
    int nfonts, fontsize;
    config_item *cfg;
    int cfg_which;
    bool cfgret;
    GtkWidget *cfgbox;
    void *paste_data;
    int paste_data_len;
    int pw, ph, ps;  /* pixmap size (w, h are area size, s is GDK scale) */
    int ox, oy;                        /* offset of pixmap in drawing area */
#ifdef OLD_FILESEL
    char *filesel_name;
#endif
    GSList *preset_radio;
    bool preset_threaded;
    GtkWidget *preset_custom;
    GtkWidget *copy_menu_item;
#if !GTK_CHECK_VERSION(3,0,0)
    bool drawing_area_shrink_pending;
    bool menubar_is_local;
#endif
#if GTK_CHECK_VERSION(3,0,0)
    /*
     * This is used to get round an annoying lack of GTK notification
     * message. If we request a window resize with
     * gtk_window_resize(), we normally get back a "configure" event
     * on the window and on its drawing area, and we respond to the
     * latter by doing an appropriate resize of the puzzle. If the
     * window is maximised, so that gtk_window_resize() _doesn't_
     * change its size, then that configure event never shows up. But
     * if we requested the resize in response to a change of puzzle
     * parameters (say, the user selected a differently-sized preset
     * from the menu), then we would still like to be _notified_ that
     * the window size was staying the same, so that we can respond by
     * choosing an appropriate tile size for the new puzzle preset in
     * the existing window size.
     *
     * Fortunately, in GTK 3, we may not get a "configure" event on
     * the drawing area in this situation, but we still get a
     * "size_allocate" event on the whole window (which, in other
     * situations when we _do_ get a "configure" on the area, turns up
     * second). So we treat _that_ event as indicating that if the
     * "configure" event hasn't already shown up then it's not going
     * to arrive.
     *
     * This flag is where we bookkeep this system. On
     * gtk_window_resize we set this flag to true; the area's
     * configure handler sets it back to false; then if that doesn't
     * happen, the window's size_allocate handler does a fallback
     * puzzle resize when it sees this flag still set to true.
     */
    bool awaiting_resize_ack;
#endif
#ifdef USE_CAIRO
    int printcount, printw, printh;
    float printscale;
    bool printsolns, printcolour;
    int hatch;
    float hatchthick, hatchspace;
    drawing *print_dr;
    document *doc;
#endif
#ifdef USE_PRINTING
    GtkPrintOperation *printop;
    GtkPrintContext *printcontext;
    GtkSpinButton *printcount_spin_button, *printw_spin_button,
        *printh_spin_button, *printscale_spin_button;
    GtkCheckButton *soln_check_button, *colour_check_button;
#endif
    const struct internal_drawing_api *dr_api;
};

struct blitter {
#ifdef USE_CAIRO
    cairo_surface_t *image;
#else
    GdkPixmap *pixmap;
#endif
    int w, h, x, y;
};

void get_random_seed(void **randseed, int *randseedsize)
{
    struct timeval *tvp = snew(struct timeval);
    gettimeofday(tvp, NULL);
    *randseed = (void *)tvp;
    *randseedsize = sizeof(struct timeval);
}

void frontend_default_colour(frontend *fe, float *output)
{
#if !GTK_CHECK_VERSION(3,0,0)
    if (!fe->headless) {
        /*
         * If we have a widget and it has a style that specifies a
         * default background colour, use that as the background for
         * the puzzle drawing area.
         */
        GdkColor col = gtk_widget_get_style(fe->window)->bg[GTK_STATE_NORMAL];
        output[0] = col.red / 65535.0;
        output[1] = col.green / 65535.0;
        output[2] = col.blue / 65535.0;
    }
#endif

    /*
     * GTK 3 has decided that there's no such thing as a 'default
     * background colour' any more, because widget styles might set
     * the background to something more complicated like a background
     * image. We don't want to get into overlaying our entire puzzle
     * on an arbitrary background image, so we'll just make up a
     * reasonable shade of grey.
     *
     * This is also what we do on GTK 2 in headless mode, where we
     * don't have a widget style to query.
     */
    output[0] = output[1] = output[2] = 0.9F;
}

static void gtk_status_bar(void *handle, const char *text)
{
    frontend *fe = (frontend *)handle;

    if (fe->headless)
        return;

    assert(fe->statusbar);

    gtk_statusbar_pop(GTK_STATUSBAR(fe->statusbar), fe->statusctx);
    gtk_statusbar_push(GTK_STATUSBAR(fe->statusbar), fe->statusctx, text);
}

/* ----------------------------------------------------------------------
 * Cairo drawing functions.
 */

#ifdef USE_CAIRO

static void setup_drawing(frontend *fe)
{
    fe->cr = cairo_create(fe->image);
    cairo_scale(fe->cr, fe->ps, fe->ps);
    cairo_set_antialias(fe->cr, CAIRO_ANTIALIAS_GRAY);
    cairo_set_line_width(fe->cr, 1.0);
    cairo_set_line_cap(fe->cr, CAIRO_LINE_CAP_SQUARE);
    cairo_set_line_join(fe->cr, CAIRO_LINE_JOIN_ROUND);
}

static void teardown_drawing(frontend *fe)
{
    cairo_destroy(fe->cr);
    fe->cr = NULL;

#ifndef USE_CAIRO_WITHOUT_PIXMAP
    if (!fe->headless) {
        cairo_t *cr = gdk_cairo_create(fe->pixmap);
        cairo_set_source_surface(cr, fe->image, 0, 0);
        cairo_rectangle(cr,
                        fe->bbox_l - 1,
                        fe->bbox_u - 1,
                        fe->bbox_r - fe->bbox_l + 2,
                        fe->bbox_d - fe->bbox_u + 2);
        cairo_fill(cr);
        cairo_destroy(cr);
    }
#endif
}

static void snaffle_colours(frontend *fe)
{
    fe->colours = midend_colours(fe->me, &fe->ncolours);
}

static void draw_set_colour(frontend *fe, int colour)
{
    cairo_set_source_rgb(fe->cr,
                         fe->colours[3*colour + 0],
                         fe->colours[3*colour + 1],
                         fe->colours[3*colour + 2]);
}

static void print_set_colour(frontend *fe, int colour)
{
    float r, g, b;

    print_get_colour(fe->print_dr, colour, fe->printcolour,
                     &(fe->hatch), &r, &g, &b);

    if (fe->hatch < 0)
        cairo_set_source_rgb(fe->cr, r, g, b);
}

static void set_window_background(frontend *fe, int colour)
{
#if GTK_CHECK_VERSION(3,0,0)
    /* In case the user's chosen theme is dark, we should not override
     * the background colour for the whole window as this makes the
     * menu and status bars unreadable. This might be visible through
     * the gtk-application-prefer-dark-theme flag or else we have to
     * work it out from the name. */
    gboolean dark_theme = false;
    char *theme_name = NULL;
    g_object_get(gtk_settings_get_default(),
		 "gtk-application-prefer-dark-theme", &dark_theme,
		 "gtk-theme-name", &theme_name,
		 NULL);
    if (theme_name && strcasestr(theme_name, "-dark"))
	dark_theme = true;
    g_free(theme_name);
#if GTK_CHECK_VERSION(3,20,0)
    char css_buf[512];
    sprintf(css_buf, ".background { "
            "background-color: #%02x%02x%02x; }",
            (unsigned)(fe->colours[3*colour + 0] * 255),
            (unsigned)(fe->colours[3*colour + 1] * 255),
            (unsigned)(fe->colours[3*colour + 2] * 255));
    if (!fe->css_provider)
        fe->css_provider = gtk_css_provider_new();
    if (!gtk_css_provider_load_from_data(
            GTK_CSS_PROVIDER(fe->css_provider), css_buf, -1, NULL))
        assert(0 && "Couldn't load CSS");
    if (!dark_theme) {
	gtk_style_context_add_provider(
	    gtk_widget_get_style_context(fe->window),
	    GTK_STYLE_PROVIDER(fe->css_provider),
	    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(fe->area),
        GTK_STYLE_PROVIDER(fe->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#else // still at least GTK 3.0 but less than 3.20
    GdkRGBA rgba;
    rgba.red = fe->colours[3*colour + 0];
    rgba.green = fe->colours[3*colour + 1];
    rgba.blue = fe->colours[3*colour + 2];
    rgba.alpha = 1.0;
    gdk_window_set_background_rgba(gtk_widget_get_window(fe->area), &rgba);
    if (!dark_theme)
	gdk_window_set_background_rgba(gtk_widget_get_window(fe->window),
				       &rgba);
#endif // GTK_CHECK_VERSION(3,20,0)
#else // GTK 2 version comes next
    GdkColormap *colmap;

    colmap = gdk_colormap_get_system();
    fe->background.red = fe->colours[3*colour + 0] * 65535;
    fe->background.green = fe->colours[3*colour + 1] * 65535;
    fe->background.blue = fe->colours[3*colour + 2] * 65535;
    if (!gdk_colormap_alloc_color(colmap, &fe->background, false, false)) {
	g_error("couldn't allocate background (#%02x%02x%02x)\n",
		fe->background.red >> 8, fe->background.green >> 8,
		fe->background.blue >> 8);
    }
    gdk_window_set_background(gtk_widget_get_window(fe->area),
                              &fe->background);
    gdk_window_set_background(gtk_widget_get_window(fe->window),
                              &fe->background);
#endif
}

static PangoLayout *make_pango_layout(frontend *fe)
{
    return (pango_cairo_create_layout(fe->cr));
}

static void draw_pango_layout(frontend *fe, PangoLayout *layout,
			      int x, int y)
{
    cairo_move_to(fe->cr, x, y);
    pango_cairo_show_layout(fe->cr, layout);
}

static void save_screenshot_png(frontend *fe, const char *screenshot_file)
{
    cairo_surface_write_to_png(fe->image, screenshot_file);
}

static void do_hatch(frontend *fe)
{
    double i, x, y, width, height, maxdim;

    /* Get the dimensions of the region to be hatched. */
    cairo_path_extents(fe->cr, &x, &y, &width, &height);

    maxdim = max(width, height);

    cairo_save(fe->cr);

    /* Set the line color and width. */
    cairo_set_source_rgb(fe->cr, 0, 0, 0);
    cairo_set_line_width(fe->cr, fe->hatchthick);
    /* Clip to the region. */
    cairo_clip(fe->cr);
    /* Hatch the bounding area of the fill region. */
    if (fe->hatch == HATCH_VERT || fe->hatch == HATCH_PLUS) {
        for (i = 0.0; i <= width; i += fe->hatchspace) {
            cairo_move_to(fe->cr, i, 0);
            cairo_rel_line_to(fe->cr, 0, height);
        }
    }
    if (fe->hatch == HATCH_HORIZ || fe->hatch == HATCH_PLUS) {
        for (i = 0.0; i <= height; i += fe->hatchspace) {
            cairo_move_to(fe->cr, 0, i);
            cairo_rel_line_to(fe->cr, width, 0);
        }
    }
    if (fe->hatch == HATCH_SLASH || fe->hatch == HATCH_X) {
        for (i = -height; i <= width; i += fe->hatchspace * ROOT2) {
            cairo_move_to(fe->cr, i, 0);
            cairo_rel_line_to(fe->cr, maxdim, maxdim);
        }
    }
    if (fe->hatch == HATCH_BACKSLASH || fe->hatch == HATCH_X) {
        for (i = 0.0; i <= width + height; i += fe->hatchspace * ROOT2) {
            cairo_move_to(fe->cr, i, 0);
            cairo_rel_line_to(fe->cr, -maxdim, maxdim);
        }
    }
    cairo_stroke(fe->cr);

    cairo_restore(fe->cr);
}

static void do_draw_fill(frontend *fe)
{
    cairo_fill(fe->cr);
}

static void do_draw_fill_preserve(frontend *fe)
{
    cairo_fill_preserve(fe->cr);
}

static void do_print_fill(frontend *fe)
{
    if (fe->hatch < 0)
        cairo_fill(fe->cr);
    else
        do_hatch(fe);
}

static void do_print_fill_preserve(frontend *fe)
{
    if (fe->hatch < 0) {
        cairo_fill_preserve(fe->cr);
    } else {
        cairo_path_t *oldpath;
        oldpath = cairo_copy_path(fe->cr);
        do_hatch(fe);
        cairo_append_path(fe->cr, oldpath);
    }
}

static void do_clip(frontend *fe, int x, int y, int w, int h)
{
    cairo_new_path(fe->cr);
    cairo_rectangle(fe->cr, x, y, w, h);
    cairo_clip(fe->cr);
}

static void do_unclip(frontend *fe)
{
    cairo_reset_clip(fe->cr);
}

static void do_draw_rect(frontend *fe, int x, int y, int w, int h)
{
    cairo_save(fe->cr);
    cairo_new_path(fe->cr);
    cairo_set_antialias(fe->cr, CAIRO_ANTIALIAS_NONE);
    cairo_rectangle(fe->cr, x, y, w, h);
    fe->dr_api->fill(fe);
    cairo_restore(fe->cr);
}

static void do_draw_line(frontend *fe, int x1, int y1, int x2, int y2)
{
    cairo_new_path(fe->cr);
    cairo_move_to(fe->cr, x1 + 0.5, y1 + 0.5);
    cairo_line_to(fe->cr, x2 + 0.5, y2 + 0.5);
    cairo_stroke(fe->cr);
}

static void do_draw_thick_line(frontend *fe, float thickness,
			       float x1, float y1, float x2, float y2)
{
    cairo_save(fe->cr);
    cairo_set_line_width(fe->cr, thickness);
    cairo_new_path(fe->cr);
    cairo_move_to(fe->cr, x1, y1);
    cairo_line_to(fe->cr, x2, y2);
    cairo_stroke(fe->cr);
    cairo_restore(fe->cr);
}

static void do_draw_poly(frontend *fe, const int *coords, int npoints,
			 int fillcolour, int outlinecolour)
{
    int i;

    cairo_new_path(fe->cr);
    for (i = 0; i < npoints; i++)
	cairo_line_to(fe->cr, coords[i*2] + 0.5, coords[i*2 + 1] + 0.5);
    cairo_close_path(fe->cr);
    if (fillcolour >= 0) {
        fe->dr_api->set_colour(fe, fillcolour);
	fe->dr_api->fill_preserve(fe);
    }
    assert(outlinecolour >= 0);
    fe->dr_api->set_colour(fe, outlinecolour);
    cairo_stroke(fe->cr);
}

static void do_draw_circle(frontend *fe, int cx, int cy, int radius,
			   int fillcolour, int outlinecolour)
{
    cairo_new_path(fe->cr);
    cairo_arc(fe->cr, cx + 0.5, cy + 0.5, radius, 0, 2*PI);
    cairo_close_path(fe->cr);		/* Just in case... */
    if (fillcolour >= 0) {
	fe->dr_api->set_colour(fe, fillcolour);
	fe->dr_api->fill_preserve(fe);
    }
    assert(outlinecolour >= 0);
    fe->dr_api->set_colour(fe, outlinecolour);
    cairo_stroke(fe->cr);
}

static void setup_blitter(blitter *bl, int w, int h)
{
    bl->image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
}

static void teardown_blitter(blitter *bl)
{
    cairo_surface_destroy(bl->image);
}

static void do_blitter_save(frontend *fe, blitter *bl, int x, int y)
{
    cairo_t *cr = cairo_create(bl->image);

    cairo_set_source_surface(cr, fe->image, -x, -y);
    cairo_paint(cr);
    cairo_destroy(cr);
}

static void do_blitter_load(frontend *fe, blitter *bl, int x, int y)
{
    cairo_set_source_surface(fe->cr, bl->image, x, y);
    cairo_paint(fe->cr);
}

static void clear_backing_store(frontend *fe)
{
    fe->image = NULL;
}

static void wipe_and_maybe_destroy_cairo(frontend *fe, cairo_t *cr,
                                         bool destroy)
{
    cairo_set_source_rgb(cr, fe->colours[0], fe->colours[1], fe->colours[2]);
    cairo_paint(cr);
    if (destroy)
        cairo_destroy(cr);
}

static void setup_backing_store(frontend *fe)
{
#ifndef USE_CAIRO_WITHOUT_PIXMAP
    if (!fe->headless) {
        fe->pixmap = gdk_pixmap_new(gtk_widget_get_window(fe->area),
                                    fe->pw*fe->ps, fe->ph*fe->ps, -1);
    } else {
        fe->pixmap = NULL;
    }
#endif

    fe->image = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
					   fe->pw*fe->ps, fe->ph*fe->ps);

    wipe_and_maybe_destroy_cairo(fe, cairo_create(fe->image), true);
#ifndef USE_CAIRO_WITHOUT_PIXMAP
    if (!fe->headless)
        wipe_and_maybe_destroy_cairo(fe, gdk_cairo_create(fe->pixmap), true);
#endif
    if (!fe->headless) {
#if GTK_CHECK_VERSION(3,22,0)
        GdkWindow *gdkwin;
        cairo_region_t *region;
        GdkDrawingContext *drawctx;
        cairo_t *cr;

        gdkwin = gtk_widget_get_window(fe->area);
        region = gdk_window_get_clip_region(gdkwin);
        drawctx = gdk_window_begin_draw_frame(gdkwin, region);
        cr = gdk_drawing_context_get_cairo_context(drawctx);
        wipe_and_maybe_destroy_cairo(fe, cr, false);
        gdk_window_end_draw_frame(gdkwin, drawctx);
        cairo_region_destroy(region);
#else
        wipe_and_maybe_destroy_cairo(
            fe, gdk_cairo_create(gtk_widget_get_window(fe->area)), true);
#endif
    }
}

static bool backing_store_ok(frontend *fe)
{
    return fe->image != NULL;
}

static void teardown_backing_store(frontend *fe)
{
    cairo_surface_destroy(fe->image);
#ifndef USE_CAIRO_WITHOUT_PIXMAP
    gdk_pixmap_unref(fe->pixmap);
#endif
    fe->image = NULL;
}

#endif

/* ----------------------------------------------------------------------
 * GDK drawing functions.
 */

#ifndef USE_CAIRO

static void setup_drawing(frontend *fe)
{
    fe->gc = gdk_gc_new(fe->area->window);
}

static void teardown_drawing(frontend *fe)
{
    gdk_gc_unref(fe->gc);
    fe->gc = NULL;
}

static void snaffle_colours(frontend *fe)
{
    int i, ncolours;
    float *colours;
    gboolean *success;

    fe->colmap = gdk_colormap_get_system();
    colours = midend_colours(fe->me, &ncolours);
    fe->ncolours = ncolours;
    fe->colours = snewn(ncolours, GdkColor);
    for (i = 0; i < ncolours; i++) {
	fe->colours[i].red = colours[i*3] * 0xFFFF;
	fe->colours[i].green = colours[i*3+1] * 0xFFFF;
	fe->colours[i].blue = colours[i*3+2] * 0xFFFF;
    }
    success = snewn(ncolours, gboolean);
    gdk_colormap_alloc_colors(fe->colmap, fe->colours, ncolours,
			      false, false, success);
    for (i = 0; i < ncolours; i++) {
	if (!success[i]) {
	    g_error("couldn't allocate colour %d (#%02x%02x%02x)\n",
		    i, fe->colours[i].red >> 8,
		    fe->colours[i].green >> 8,
		    fe->colours[i].blue >> 8);
	}
    }
}

static void set_window_background(frontend *fe, int colour)
{
    fe->backgroundindex = colour;
    gdk_window_set_background(fe->area->window, &fe->colours[colour]);
    gdk_window_set_background(fe->window->window, &fe->colours[colour]);
}

static void draw_set_colour(frontend *fe, int colour)
{
    gdk_gc_set_foreground(fe->gc, &fe->colours[colour]);
}

#ifdef USE_PANGO
static PangoLayout *make_pango_layout(frontend *fe)
{
    return (pango_layout_new(gtk_widget_get_pango_context(fe->area)));
}

static void draw_pango_layout(frontend *fe, PangoLayout *layout,
			      int x, int y)
{
    gdk_draw_layout(fe->pixmap, fe->gc, x, y, layout);
}
#endif

static void save_screenshot_png(frontend *fe, const char *screenshot_file)
{
    GdkPixbuf *pb;
    GError *gerror = NULL;

    midend_redraw(fe->me);

    pb = gdk_pixbuf_get_from_drawable(NULL, fe->pixmap,
				      NULL, 0, 0, 0, 0, -1, -1);
    gdk_pixbuf_save(pb, screenshot_file, "png", &gerror, NULL);
}

static void do_clip(frontend *fe, int x, int y, int w, int h)
{
    GdkRectangle rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    gdk_gc_set_clip_rectangle(fe->gc, &rect);
}

static void do_unclip(frontend *fe)
{
    GdkRectangle rect;

    rect.x = 0;
    rect.y = 0;
    rect.width = fe->w;
    rect.height = fe->h;
    gdk_gc_set_clip_rectangle(fe->gc, &rect);
}

static void do_draw_rect(frontend *fe, int x, int y, int w, int h)
{
    gdk_draw_rectangle(fe->pixmap, fe->gc, 1, x, y, w, h);
}

static void do_draw_line(frontend *fe, int x1, int y1, int x2, int y2)
{
    gdk_draw_line(fe->pixmap, fe->gc, x1, y1, x2, y2);
}

static void do_draw_thick_line(frontend *fe, float thickness,
			       float x1, float y1, float x2, float y2)
{
    GdkGCValues save;

    gdk_gc_get_values(fe->gc, &save);
    gdk_gc_set_line_attributes(fe->gc,
			       thickness,
			       GDK_LINE_SOLID,
			       GDK_CAP_BUTT,
			       GDK_JOIN_BEVEL);
    gdk_draw_line(fe->pixmap, fe->gc, x1, y1, x2, y2);
    gdk_gc_set_line_attributes(fe->gc,
			       save.line_width,
			       save.line_style,
			       save.cap_style,
			       save.join_style);
}

static void do_draw_poly(frontend *fe, const int *coords, int npoints,
			 int fillcolour, int outlinecolour)
{
    GdkPoint *points = snewn(npoints, GdkPoint);
    int i;

    for (i = 0; i < npoints; i++) {
        points[i].x = coords[i*2];
        points[i].y = coords[i*2+1];
    }

    if (fillcolour >= 0) {
	fe->dr_api->set_colour(fe, fillcolour);
	gdk_draw_polygon(fe->pixmap, fe->gc, true, points, npoints);
    }
    assert(outlinecolour >= 0);
    fe->dr_api->set_colour(fe, outlinecolour);

    /*
     * In principle we ought to be able to use gdk_draw_polygon for
     * the outline as well. In fact, it turns out to interact badly
     * with a clipping region, for no terribly obvious reason, so I
     * draw the outline as a sequence of lines instead.
     */
    for (i = 0; i < npoints; i++)
	gdk_draw_line(fe->pixmap, fe->gc,
		      points[i].x, points[i].y,
		      points[(i+1)%npoints].x, points[(i+1)%npoints].y);

    sfree(points);
}

static void do_draw_circle(frontend *fe, int cx, int cy, int radius,
			   int fillcolour, int outlinecolour)
{
    if (fillcolour >= 0) {
	fe->dr_api->set_colour(fe, fillcolour);
	gdk_draw_arc(fe->pixmap, fe->gc, true,
		     cx - radius, cy - radius,
		     2 * radius, 2 * radius, 0, 360 * 64);
    }

    assert(outlinecolour >= 0);
    fe->dr_api->set_colour(fe, outlinecolour);
    gdk_draw_arc(fe->pixmap, fe->gc, false,
		 cx - radius, cy - radius,
		 2 * radius, 2 * radius, 0, 360 * 64);
}

static void setup_blitter(blitter *bl, int w, int h)
{
    /*
     * We can't create the pixmap right now, because fe->window
     * might not yet exist. So we just cache w and h and create it
     * during the firs call to blitter_save.
     */
    bl->pixmap = NULL;
}

static void teardown_blitter(blitter *bl)
{
    if (bl->pixmap)
	gdk_pixmap_unref(bl->pixmap);
}

static void do_blitter_save(frontend *fe, blitter *bl, int x, int y)
{
    if (!bl->pixmap)
        bl->pixmap = gdk_pixmap_new(fe->area->window, bl->w, bl->h, -1);
    gdk_draw_pixmap(bl->pixmap,
                    fe->area->style->fg_gc[GTK_WIDGET_STATE(fe->area)],
                    fe->pixmap,
                    x, y, 0, 0, bl->w, bl->h);
}

static void do_blitter_load(frontend *fe, blitter *bl, int x, int y)
{
    assert(bl->pixmap);
    gdk_draw_pixmap(fe->pixmap,
                    fe->area->style->fg_gc[GTK_WIDGET_STATE(fe->area)],
                    bl->pixmap,
                    0, 0, x, y, bl->w, bl->h);
}

static void clear_backing_store(frontend *fe)
{
    fe->pixmap = NULL;
}

static void setup_backing_store(frontend *fe)
{
    GdkGC *gc;

    if (fe->headless) {
        fprintf(stderr, "headless mode does not work with GDK drawing\n");
        exit(1);
    }

    fe->pixmap = gdk_pixmap_new(fe->area->window, fe->pw, fe->ph, -1);

    gc = gdk_gc_new(fe->area->window);
    gdk_gc_set_foreground(gc, &fe->colours[0]);
    gdk_draw_rectangle(fe->pixmap, gc, 1, 0, 0, fe->pw, fe->ph);
    gdk_draw_rectangle(fe->area->window, gc, 1, 0, 0, fe->w, fe->h);
    gdk_gc_unref(gc);
}

static int backing_store_ok(frontend *fe)
{
    return (!!fe->pixmap);
}

static void teardown_backing_store(frontend *fe)
{
    gdk_pixmap_unref(fe->pixmap);
    fe->pixmap = NULL;
}

#endif

#ifndef USE_CAIRO_WITHOUT_PIXMAP
static void repaint_rectangle(frontend *fe, GtkWidget *widget,
			      int x, int y, int w, int h)
{
    GdkGC *gc = gdk_gc_new(gtk_widget_get_window(widget));
#ifdef USE_CAIRO
    gdk_gc_set_foreground(gc, &fe->background);
#else
    gdk_gc_set_foreground(gc, &fe->colours[fe->backgroundindex]);
#endif
    if (x < fe->ox) {
	gdk_draw_rectangle(gtk_widget_get_window(widget), gc,
			   true, x, y, fe->ox - x, h);
	w -= (fe->ox - x);
	x = fe->ox;
    }
    if (y < fe->oy) {
	gdk_draw_rectangle(gtk_widget_get_window(widget), gc,
			   true, x, y, w, fe->oy - y);
	h -= (fe->oy - y);
	y = fe->oy;
    }
    if (w > fe->pw) {
	gdk_draw_rectangle(gtk_widget_get_window(widget), gc,
			   true, x + fe->pw, y, w - fe->pw, h);
	w = fe->pw;
    }
    if (h > fe->ph) {
	gdk_draw_rectangle(gtk_widget_get_window(widget), gc,
			   true, x, y + fe->ph, w, h - fe->ph);
	h = fe->ph;
    }
    gdk_draw_pixmap(gtk_widget_get_window(widget), gc, fe->pixmap,
		    x - fe->ox, y - fe->oy, x, y, w, h);
    gdk_gc_unref(gc);
}
#endif

/* ----------------------------------------------------------------------
 * Pango font functions.
 */

#ifdef USE_PANGO

static void add_font(frontend *fe, int index, int fonttype, int fontsize)
{
    /*
     * Use Pango to find the closest match to the requested
     * font.
     */
    PangoFontDescription *fd;

    fd = pango_font_description_new();
    /* `Monospace' and `Sans' are meta-families guaranteed to exist */
    pango_font_description_set_family(fd, fonttype == FONT_FIXED ?
				      "Monospace" : "Sans");
    pango_font_description_set_weight(fd, PANGO_WEIGHT_BOLD);
    /*
     * I found some online Pango documentation which
     * described a function called
     * pango_font_description_set_absolute_size(), which is
     * _exactly_ what I want here. Unfortunately, none of
     * my local Pango installations have it (presumably
     * they're too old), so I'm going to have to hack round
     * it by figuring out the point size myself. This
     * limits me to X and probably also breaks in later
     * Pango installations, so ideally I should add another
     * CHECK_VERSION type ifdef and use set_absolute_size
     * where available. All very annoying.
     */
#ifdef HAVE_SENSIBLE_ABSOLUTE_SIZE_FUNCTION
    pango_font_description_set_absolute_size(fd, PANGO_SCALE*fontsize);
#else
    {
	Display *d = GDK_DISPLAY();
	int s = DefaultScreen(d);
	double resolution =
	    (PANGO_SCALE * 72.27 / 25.4) *
	    ((double) DisplayWidthMM(d, s) / DisplayWidth (d, s));
	pango_font_description_set_size(fd, resolution * fontsize);
    }
#endif
    fe->fonts[index].desc = fd;
}

static void align_and_draw_text(frontend *fe,
				int index, int align, int x, int y,
				const char *text)
{
    PangoLayout *layout;
    PangoRectangle rect;

    layout = make_pango_layout(fe);

    /*
     * Create a layout.
     */
    pango_layout_set_font_description(layout, fe->fonts[index].desc);
    pango_layout_set_text(layout, text, strlen(text));
    pango_layout_get_pixel_extents(layout, NULL, &rect);

    if (align & ALIGN_VCENTRE)
	rect.y -= rect.height / 2;
    else
	rect.y -= rect.height;

    if (align & ALIGN_HCENTRE)
	rect.x -= rect.width / 2;
    else if (align & ALIGN_HRIGHT)
	rect.x -= rect.width;

    draw_pango_layout(fe, layout, rect.x + x, rect.y + y);

    g_object_unref(layout);
}

#endif

/* ----------------------------------------------------------------------
 * Old-fashioned font functions.
 */

#ifndef USE_PANGO

static void add_font(int index, int fonttype, int fontsize)
{
    /*
     * In GTK 1.2, I don't know of any plausible way to
     * pick a suitable font, so I'm just going to be
     * tedious.
     */
    fe->fonts[i].font = gdk_font_load(fonttype == FONT_FIXED ?
				      "fixed" : "variable");
}

static void align_and_draw_text(int index, int align, int x, int y,
				const char *text)
{
    int lb, rb, wid, asc, desc;

    /*
     * Measure vertical string extents with respect to the same
     * string always...
     */
    gdk_string_extents(fe->fonts[i].font,
		       "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
		       &lb, &rb, &wid, &asc, &desc);
    if (align & ALIGN_VCENTRE)
	y += asc - (asc+desc)/2;
    else
	y += asc;

    /*
     * ... but horizontal extents with respect to the provided
     * string. This means that multiple pieces of text centred
     * on the same y-coordinate don't have different baselines.
     */
    gdk_string_extents(fe->fonts[i].font, text,
		       &lb, &rb, &wid, &asc, &desc);

    if (align & ALIGN_HCENTRE)
	x -= wid / 2;
    else if (align & ALIGN_HRIGHT)
	x -= wid;

    /*
     * Actually draw the text.
     */
    gdk_draw_string(fe->pixmap, fe->fonts[i].font, fe->gc, x, y, text);
}

#endif

/* ----------------------------------------------------------------------
 * The exported drawing functions.
 */

static void gtk_start_draw(void *handle)
{
    frontend *fe = (frontend *)handle;
    fe->bbox_l = fe->w;
    fe->bbox_r = 0;
    fe->bbox_u = fe->h;
    fe->bbox_d = 0;
    setup_drawing(fe);
}

static void gtk_clip(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    do_clip(fe, x, y, w, h);
}

static void gtk_unclip(void *handle)
{
    frontend *fe = (frontend *)handle;
    do_unclip(fe);
}

static void gtk_draw_text(void *handle, int x, int y, int fonttype,
                          int fontsize, int align, int colour,
                          const char *text)
{
    frontend *fe = (frontend *)handle;
    int i;

    /*
     * Find or create the font.
     */
    for (i = 0; i < fe->nfonts; i++)
        if (fe->fonts[i].type == fonttype && fe->fonts[i].size == fontsize)
            break;

    if (i == fe->nfonts) {
        if (fe->fontsize <= fe->nfonts) {
            fe->fontsize = fe->nfonts + 10;
            fe->fonts = sresize(fe->fonts, fe->fontsize, struct font);
        }

        fe->nfonts++;

        fe->fonts[i].type = fonttype;
        fe->fonts[i].size = fontsize;
	add_font(fe, i, fonttype, fontsize);
    }

    /*
     * Do the job.
     */
    fe->dr_api->set_colour(fe, colour);
    align_and_draw_text(fe, i, align, x, y, text);
}

static void gtk_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
    frontend *fe = (frontend *)handle;
    fe->dr_api->set_colour(fe, colour);
    do_draw_rect(fe, x, y, w, h);
}

static void gtk_draw_line(void *handle, int x1, int y1, int x2, int y2,
                          int colour)
{
    frontend *fe = (frontend *)handle;
    fe->dr_api->set_colour(fe, colour);
    do_draw_line(fe, x1, y1, x2, y2);
}

static void gtk_draw_thick_line(void *handle, float thickness,
                                float x1, float y1, float x2, float y2,
                                int colour)
{
    frontend *fe = (frontend *)handle;
    fe->dr_api->set_colour(fe, colour);
    do_draw_thick_line(fe, thickness, x1, y1, x2, y2);
}

static void gtk_draw_poly(void *handle, const int *coords, int npoints,
                          int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    do_draw_poly(fe, coords, npoints, fillcolour, outlinecolour);
}

static void gtk_draw_circle(void *handle, int cx, int cy, int radius,
                            int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    do_draw_circle(fe, cx, cy, radius, fillcolour, outlinecolour);
}

static blitter *gtk_blitter_new(void *handle, int w, int h)
{
    blitter *bl = snew(blitter);
    setup_blitter(bl, w, h);
    bl->w = w;
    bl->h = h;
    return bl;
}

static void gtk_blitter_free(void *handle, blitter *bl)
{
    teardown_blitter(bl);
    sfree(bl);
}

static void gtk_blitter_save(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    do_blitter_save(fe, bl, x, y);
    bl->x = x;
    bl->y = y;
}

static void gtk_blitter_load(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    if (x == BLITTER_FROMSAVED && y == BLITTER_FROMSAVED) {
        x = bl->x;
        y = bl->y;
    }
    do_blitter_load(fe, bl, x, y);
}

static void gtk_draw_update(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    if (fe->bbox_l > x  ) fe->bbox_l = x  ;
    if (fe->bbox_r < x+w) fe->bbox_r = x+w;
    if (fe->bbox_u > y  ) fe->bbox_u = y  ;
    if (fe->bbox_d < y+h) fe->bbox_d = y+h;
}

static void gtk_end_draw(void *handle)
{
    frontend *fe = (frontend *)handle;

    teardown_drawing(fe);

    if (fe->bbox_l < fe->bbox_r && fe->bbox_u < fe->bbox_d && !fe->headless) {
#ifdef USE_CAIRO_WITHOUT_PIXMAP
        gtk_widget_queue_draw_area(fe->area,
                                   fe->bbox_l - 1 + fe->ox,
                                   fe->bbox_u - 1 + fe->oy,
                                   fe->bbox_r - fe->bbox_l + 2,
                                   fe->bbox_d - fe->bbox_u + 2);
#else
	repaint_rectangle(fe, fe->area,
			  fe->bbox_l - 1 + fe->ox,
			  fe->bbox_u - 1 + fe->oy,
			  fe->bbox_r - fe->bbox_l + 2,
			  fe->bbox_d - fe->bbox_u + 2);
#endif
    }
}

#ifdef USE_PANGO
static char *gtk_text_fallback(void *handle, const char *const *strings,
                               int nstrings)
{
    /*
     * We assume Pango can cope with any UTF-8 likely to be emitted
     * by a puzzle.
     */
    return dupstr(strings[0]);
}
#endif

#ifdef USE_PRINTING
static void gtk_begin_doc(void *handle, int pages)
{
    frontend *fe = (frontend *)handle;
    gtk_print_operation_set_n_pages(fe->printop, pages);
}

static void gtk_begin_page(void *handle, int number)
{
}

static void gtk_begin_puzzle(void *handle, float xm, float xc,
                             float ym, float yc, int pw, int ph, float wmm)
{
    frontend *fe = (frontend *)handle;
    double ppw, pph, pox, poy, dpmmx, dpmmy;
    double scale;

    ppw = gtk_print_context_get_width(fe->printcontext);
    pph = gtk_print_context_get_height(fe->printcontext);
    dpmmx = gtk_print_context_get_dpi_x(fe->printcontext) / 25.4;
    dpmmy = gtk_print_context_get_dpi_y(fe->printcontext) / 25.4;

    /*
     * Compute the puzzle's position in pixels on the logical page.
     */
    pox = xm * ppw + xc * dpmmx;
    poy = ym * pph + yc * dpmmy;

    /*
     * And determine the scale.
     *
     * I need a scale such that the maximum puzzle-coordinate
     * extent of the rectangle (pw * scale) is equal to the pixel
     * equivalent of the puzzle's millimetre width (wmm * dpmmx).
     */
    scale = wmm * dpmmx / pw;

    /*
     * Now instruct Cairo to transform points based on our calculated
     * values (order here *is* important).
     */
    cairo_save(fe->cr);
    cairo_translate(fe->cr, pox, poy);
    cairo_scale(fe->cr, scale, scale);

    fe->hatchthick = 0.2 * pw / wmm;
    fe->hatchspace = 1.0 * pw / wmm;
}

static void gtk_end_puzzle(void *handle)
{
    frontend *fe = (frontend *)handle;
    cairo_restore(fe->cr);
}

static void gtk_end_page(void *handle, int number)
{
}

static void gtk_end_doc(void *handle)
{
}

static void gtk_line_width(void *handle, float width)
{
    frontend *fe = (frontend *)handle;
    cairo_set_line_width(fe->cr, width);
}

static void gtk_line_dotted(void *handle, bool dotted)
{
    frontend *fe = (frontend *)handle;

    if (dotted) {
        const double dash = 35.0;
        cairo_set_dash(fe->cr, &dash, 1, 0);
    } else {
        cairo_set_dash(fe->cr, NULL, 0, 0);
    }
}
#endif /* USE_PRINTING */

static const struct internal_drawing_api internal_drawing = {
    draw_set_colour,
#ifdef USE_CAIRO
    do_draw_fill,
    do_draw_fill_preserve,
#endif
};

#ifdef USE_CAIRO
static const struct internal_drawing_api internal_printing = {
    print_set_colour,
    do_print_fill,
    do_print_fill_preserve,
};
#endif

static const struct drawing_api gtk_drawing = {
    gtk_draw_text,
    gtk_draw_rect,
    gtk_draw_line,
    gtk_draw_poly,
    gtk_draw_circle,
    gtk_draw_update,
    gtk_clip,
    gtk_unclip,
    gtk_start_draw,
    gtk_end_draw,
    gtk_status_bar,
    gtk_blitter_new,
    gtk_blitter_free,
    gtk_blitter_save,
    gtk_blitter_load,
#ifdef USE_PRINTING
    gtk_begin_doc,
    gtk_begin_page,
    gtk_begin_puzzle,
    gtk_end_puzzle,
    gtk_end_page,
    gtk_end_doc,
    gtk_line_width,
    gtk_line_dotted,
#else
    NULL, NULL, NULL, NULL, NULL, NULL, /* {begin,end}_{doc,page,puzzle} */
    NULL, NULL,			       /* line_width, line_dotted */
#endif
#ifdef USE_PANGO
    gtk_text_fallback,
#else
    NULL,
#endif
#ifdef NO_THICK_LINE
    NULL,
#else
    gtk_draw_thick_line,
#endif
};

static void destroy(GtkWidget *widget, gpointer data)
{
    frontend *fe = (frontend *)data;
    deactivate_timer(fe);
    midend_free(fe->me);
    gtk_main_quit();
}

static gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    frontend *fe = (frontend *)data;
    int keyval;
    int shift = (event->state & GDK_SHIFT_MASK) ? MOD_SHFT : 0;
    int ctrl = (event->state & GDK_CONTROL_MASK) ? MOD_CTRL : 0;

    if (!backing_store_ok(fe))
        return true;

    /* Handle mnemonics. */
    if (gtk_window_activate_key(GTK_WINDOW(fe->window), event))
        return true;

    if (event->keyval == GDK_KEY_Up)
        keyval = shift | ctrl | CURSOR_UP;
    else if (event->keyval == GDK_KEY_KP_Up ||
             event->keyval == GDK_KEY_KP_8)
	keyval = MOD_NUM_KEYPAD | '8';
    else if (event->keyval == GDK_KEY_Down)
        keyval = shift | ctrl | CURSOR_DOWN;
    else if (event->keyval == GDK_KEY_KP_Down ||
             event->keyval == GDK_KEY_KP_2)
	keyval = MOD_NUM_KEYPAD | '2';
    else if (event->keyval == GDK_KEY_Left)
        keyval = shift | ctrl | CURSOR_LEFT;
    else if (event->keyval == GDK_KEY_KP_Left ||
             event->keyval == GDK_KEY_KP_4)
	keyval = MOD_NUM_KEYPAD | '4';
    else if (event->keyval == GDK_KEY_Right)
        keyval = shift | ctrl | CURSOR_RIGHT;
    else if (event->keyval == GDK_KEY_KP_Right ||
             event->keyval == GDK_KEY_KP_6)
	keyval = MOD_NUM_KEYPAD | '6';
    else if (event->keyval == GDK_KEY_KP_Home ||
             event->keyval == GDK_KEY_KP_7)
        keyval = MOD_NUM_KEYPAD | '7';
    else if (event->keyval == GDK_KEY_KP_End ||
             event->keyval == GDK_KEY_KP_1)
        keyval = MOD_NUM_KEYPAD | '1';
    else if (event->keyval == GDK_KEY_KP_Page_Up ||
             event->keyval == GDK_KEY_KP_9)
        keyval = MOD_NUM_KEYPAD | '9';
    else if (event->keyval == GDK_KEY_KP_Page_Down ||
             event->keyval == GDK_KEY_KP_3)
        keyval = MOD_NUM_KEYPAD | '3';
    else if (event->keyval == GDK_KEY_KP_Insert ||
             event->keyval == GDK_KEY_KP_0)
        keyval = MOD_NUM_KEYPAD | '0';
    else if (event->keyval == GDK_KEY_KP_Begin ||
             event->keyval == GDK_KEY_KP_5)
        keyval = MOD_NUM_KEYPAD | '5';
    else if (event->keyval == GDK_KEY_BackSpace ||
	     event->keyval == GDK_KEY_Delete ||
	     event->keyval == GDK_KEY_KP_Delete)
        keyval = '\177';
    else if ((event->keyval == 'z' || event->keyval == 'Z') && shift && ctrl)
        keyval = UI_REDO;
    else if (event->string[0] && !event->string[1])
        keyval = (unsigned char)event->string[0];
    else
        keyval = -1;

    if (keyval >= 0 &&
        !midend_process_key(fe->me, 0, 0, keyval, NULL))
	gtk_widget_destroy(fe->window);

    return true;
}

static gint button_event(GtkWidget *widget, GdkEventButton *event,
                         gpointer data)
{
    frontend *fe = (frontend *)data;
    int button;

    if (!backing_store_ok(fe))
        return true;

    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE)
        return true;

    if (event->button == 2 || (event->state & GDK_SHIFT_MASK))
	button = MIDDLE_BUTTON;
    else if (event->button == 3 || (event->state & GDK_MOD1_MASK))
	button = RIGHT_BUTTON;
    else if (event->button == 1)
	button = LEFT_BUTTON;
    else if (event->button == 8 && event->type == GDK_BUTTON_PRESS)
        button = 'u';
    else if (event->button == 9 && event->type == GDK_BUTTON_PRESS)
        button = 'r';
    else
	return false;		       /* don't even know what button! */

    if (event->type == GDK_BUTTON_RELEASE && button >= LEFT_BUTTON)
        button += LEFT_RELEASE - LEFT_BUTTON;

    if (!midend_process_key(fe->me, event->x - fe->ox,
                            event->y - fe->oy, button, NULL))
	gtk_widget_destroy(fe->window);

    return true;
}

static gint motion_event(GtkWidget *widget, GdkEventMotion *event,
                         gpointer data)
{
    frontend *fe = (frontend *)data;
    int button;

    if (!backing_store_ok(fe))
        return true;

    if (event->state & (GDK_BUTTON2_MASK | GDK_SHIFT_MASK))
	button = MIDDLE_DRAG;
    else if (event->state & GDK_BUTTON1_MASK)
	button = LEFT_DRAG;
    else if (event->state & GDK_BUTTON3_MASK)
	button = RIGHT_DRAG;
    else
	return false;		       /* don't even know what button! */

    if (!midend_process_key(fe->me, event->x - fe->ox,
                            event->y - fe->oy, button, NULL))
	gtk_widget_destroy(fe->window);
#if GTK_CHECK_VERSION(2,12,0)
    gdk_event_request_motions(event);
#else
    gdk_window_get_pointer(gtk_widget_get_window(widget), NULL, NULL, NULL);
#endif

    return true;
}

#if GTK_CHECK_VERSION(3,0,0)
static gint draw_area(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    frontend *fe = (frontend *)data;
    GdkRectangle dirtyrect;

    cairo_surface_t *target_surface = cairo_get_target(cr);
    cairo_matrix_t m;
    cairo_get_matrix(cr, &m);
    double orig_sx, orig_sy;
    cairo_surface_get_device_scale(target_surface, &orig_sx, &orig_sy);
    cairo_surface_set_device_scale(target_surface, 1.0, 1.0);
    cairo_translate(cr, m.x0 * (orig_sx - 1.0), m.y0 * (orig_sy - 1.0));

    gdk_cairo_get_clip_rectangle(cr, &dirtyrect);
    cairo_set_source_surface(cr, fe->image, fe->ox, fe->oy);
    cairo_rectangle(cr, dirtyrect.x, dirtyrect.y,
                    dirtyrect.width, dirtyrect.height);
    cairo_fill(cr);

    cairo_surface_set_device_scale(target_surface, orig_sx, orig_sy);

    return true;
}
#else
static gint expose_area(GtkWidget *widget, GdkEventExpose *event,
                        gpointer data)
{
    frontend *fe = (frontend *)data;

    if (backing_store_ok(fe)) {
#ifdef USE_CAIRO_WITHOUT_PIXMAP
        cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));
        cairo_set_source_surface(cr, fe->image, fe->ox, fe->oy);
        cairo_rectangle(cr, event->area.x, event->area.y,
                        event->area.width, event->area.height);
        cairo_fill(cr);
        cairo_destroy(cr);
#else
	repaint_rectangle(fe, widget,
			  event->area.x, event->area.y,
			  event->area.width, event->area.height);
#endif
    }
    return true;
}
#endif

static gint map_window(GtkWidget *widget, GdkEvent *event,
		       gpointer data)
{
    frontend *fe = (frontend *)data;

    /*
     * Apparently we need to do this because otherwise the status
     * bar will fail to update immediately. Annoying, but there we
     * go.
     */
    gtk_widget_queue_draw(fe->window);

    return true;
}

static void resize_puzzle_to_area(frontend *fe, int x, int y)
{
    int oldw = fe->w, oldpw = fe->pw, oldh = fe->h, oldph = fe->ph;
    int oldps = fe->ps;

    fe->w = x;
    fe->h = y;
    midend_size(fe->me, &x, &y, true, 1.0);
    fe->pw = x;
    fe->ph = y;
#if GTK_CHECK_VERSION(3,10,0)
    fe->ps = gtk_widget_get_scale_factor(fe->area);
#else
    fe->ps = 1;
#endif
    fe->ox = (fe->w - fe->pw) / 2;
    fe->oy = (fe->h - fe->ph) / 2;

    if (oldw != fe->w || oldpw != fe->pw || oldps != fe->ps ||
        oldh != fe->h || oldph != fe->ph || !backing_store_ok(fe)) {
        if (backing_store_ok(fe))
            teardown_backing_store(fe);
        setup_backing_store(fe);
    }

    midend_force_redraw(fe->me);
}

static gint configure_area(GtkWidget *widget,
                           GdkEventConfigure *event, gpointer data)
{
    frontend *fe = (frontend *)data;

    resize_puzzle_to_area(fe, event->width, event->height);
#if GTK_CHECK_VERSION(3,0,0)
    fe->awaiting_resize_ack = false;
#endif
    return true;
}

#if GTK_CHECK_VERSION(3,0,0)
static void window_size_alloc(GtkWidget *widget, GtkAllocation *allocation,
                              gpointer data)
{
    frontend *fe = (frontend *)data;
    if (fe->awaiting_resize_ack) {
        GtkAllocation a;
        gtk_widget_get_allocation(fe->area, &a);
        resize_puzzle_to_area(fe, a.width, a.height);
        fe->awaiting_resize_ack = false;
    }
}
#endif

static gint timer_func(gpointer data)
{
    frontend *fe = (frontend *)data;

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
    if (!fe)
	return;			       /* can happen due to --generate */
    if (fe->timer_active)
        g_source_remove(fe->timer_id);
    fe->timer_active = false;
}

void activate_timer(frontend *fe)
{
    if (!fe)
	return;			       /* can happen due to --generate */
    if (!fe->timer_active) {
        fe->timer_id = g_timeout_add(20, timer_func, fe);
	gettimeofday(&fe->last_time, NULL);
    }
    fe->timer_active = true;
}

static void window_destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

static gint win_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GObject *cancelbutton = G_OBJECT(data);

    /*
     * `Escape' effectively clicks the cancel button
     */
    if (event->keyval == GDK_KEY_Escape) {
	g_signal_emit_by_name(cancelbutton, "clicked");
	return true;
    }

    return false;
}

enum { MB_OK, MB_YESNO };

static void align_label(GtkLabel *label, double x, double y)
{
#if GTK_CHECK_VERSION(3,16,0)
    gtk_label_set_xalign(label, x);
    gtk_label_set_yalign(label, y);
#elif GTK_CHECK_VERSION(3,14,0)
    gtk_widget_set_halign(GTK_WIDGET(label),
                          x == 0 ? GTK_ALIGN_START :
                          x == 1 ? GTK_ALIGN_END : GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(label),
                          y == 0 ? GTK_ALIGN_START :
                          y == 1 ? GTK_ALIGN_END : GTK_ALIGN_CENTER);
#else
    gtk_misc_set_alignment(GTK_MISC(label), x, y);
#endif
}

#if GTK_CHECK_VERSION(3,0,0)
static bool message_box(GtkWidget *parent, const char *title, const char *msg,
                        bool centre, int type)
{
    GtkWidget *window;
    gint ret;

    window = gtk_message_dialog_new
        (GTK_WINDOW(parent),
         (GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
         (type == MB_OK ? GTK_MESSAGE_INFO : GTK_MESSAGE_QUESTION),
         (type == MB_OK ? GTK_BUTTONS_OK   : GTK_BUTTONS_YES_NO),
         "%s", msg);
    gtk_window_set_title(GTK_WINDOW(window), title);
    ret = gtk_dialog_run(GTK_DIALOG(window));
    gtk_widget_destroy(window);
    return (type == MB_OK ? true : (ret == GTK_RESPONSE_YES));
}
#else /* GTK_CHECK_VERSION(3,0,0) */
static void msgbox_button_clicked(GtkButton *button, gpointer data)
{
    GtkWidget *window = GTK_WIDGET(data);
    int v, *ip;

    ip = (int *)g_object_get_data(G_OBJECT(window), "user-data");
    v = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "user-data"));
    *ip = v;

    gtk_widget_destroy(GTK_WIDGET(data));
}

bool message_box(GtkWidget *parent, const char *title, const char *msg,
                 bool centre, int type)
{
    GtkWidget *window, *hbox, *text, *button;
    const char *titles;
    int i, def, cancel;

    window = gtk_dialog_new();
    text = gtk_label_new(msg);
    align_label(GTK_LABEL(text), 0.0, 0.0);
    hbox = gtk_hbox_new(false, 0);
    gtk_box_pack_start(GTK_BOX(hbox), text, false, false, 20);
    gtk_box_pack_start
        (GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(window))),
         hbox, false, false, 20);
    gtk_widget_show(text);
    gtk_widget_show(hbox);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_label_set_line_wrap(GTK_LABEL(text), true);

    if (type == MB_OK) {
	titles = LABEL_OK "\0";
	def = cancel = 0;
    } else {
	assert(type == MB_YESNO);
	titles = LABEL_NO "\0" LABEL_YES "\0";
	def = 1;
	cancel = 0;
    }
    i = 0;
    
    while (*titles) {
	button = gtk_button_new_with_our_label(titles);
	gtk_box_pack_end
            (GTK_BOX(gtk_dialog_get_action_area(GTK_DIALOG(window))),
             button, false, false, 0);
	gtk_widget_show(button);
	if (i == def) {
	    gtk_widget_set_can_default(button, true);
	    gtk_window_set_default(GTK_WINDOW(window), button);
	}
	if (i == cancel) {
	    g_signal_connect(G_OBJECT(window), "key_press_event",
                             G_CALLBACK(win_key_press), button);
	}
	g_signal_connect(G_OBJECT(button), "clicked",
                         G_CALLBACK(msgbox_button_clicked), window);
	g_object_set_data(G_OBJECT(button), "user-data",
                          GINT_TO_POINTER(i));
	titles += strlen(titles)+1;
	i++;
    }
    g_object_set_data(G_OBJECT(window), "user-data", &i);
    g_signal_connect(G_OBJECT(window), "destroy",
                     G_CALLBACK(window_destroy), NULL);
    gtk_window_set_modal(GTK_WINDOW(window), true);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent));
    /* set_transient_window_pos(parent, window); */
    gtk_widget_show(window);
    i = -1;
    gtk_main();
    return (type == MB_YESNO ? i == 1 : true);
}
#endif /* GTK_CHECK_VERSION(3,0,0) */

static void error_box(GtkWidget *parent, const char *msg)
{
    message_box(parent, "Error", msg, false, MB_OK);
}

static void config_ok_button_clicked(GtkButton *button, gpointer data)
{
    frontend *fe = (frontend *)data;
    const char *err;

    err = midend_set_config(fe->me, fe->cfg_which, fe->cfg);

    if (err)
	error_box(fe->cfgbox, err);
    else {
	fe->cfgret = true;
	gtk_widget_destroy(fe->cfgbox);
	changed_preset(fe);
    }
}

static void config_cancel_button_clicked(GtkButton *button, gpointer data)
{
    frontend *fe = (frontend *)data;

    gtk_widget_destroy(fe->cfgbox);
}

static gint editbox_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    /*
     * GtkEntry has a nasty habit of eating the Return key, which
     * is unhelpful since it doesn't actually _do_ anything with it
     * (it calls gtk_widget_activate, but our edit boxes never need
     * activating). So I catch Return before GtkEntry sees it, and
     * pass it straight on to the parent widget. Effect: hitting
     * Return in an edit box will now activate the default button
     * in the dialog just like it will everywhere else.
     */
    if (event->keyval == GDK_KEY_Return &&
        gtk_widget_get_parent(widget) != NULL) {
	gint return_val;
	g_signal_stop_emission_by_name(G_OBJECT(widget), "key_press_event");
	g_signal_emit_by_name(G_OBJECT(gtk_widget_get_parent(widget)),
                              "key_press_event", event, &return_val);
	return return_val;
    }
    return false;
}

static void editbox_changed(GtkEditable *ed, gpointer data)
{
    config_item *i = (config_item *)data;

    assert(i->type == C_STRING);
    sfree(i->u.string.sval);
    i->u.string.sval = dupstr(gtk_entry_get_text(GTK_ENTRY(ed)));
}

static void button_toggled(GtkToggleButton *tb, gpointer data)
{
    config_item *i = (config_item *)data;

    assert(i->type == C_BOOLEAN);
    i->u.boolean.bval = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tb));
}

static void droplist_sel(GtkComboBox *combo, gpointer data)
{
    config_item *i = (config_item *)data;

    assert(i->type == C_CHOICES);
    i->u.choices.selected = gtk_combo_box_get_active(combo);
}

static bool get_config(frontend *fe, int which)
{
    GtkWidget *w, *table, *cancel;
    GtkBox *content_box, *button_box;
    char *title;
    config_item *i;
    int y;

    fe->cfg = midend_get_config(fe->me, which, &title);
    fe->cfg_which = which;
    fe->cfgret = false;

#if GTK_CHECK_VERSION(3,0,0)
    /* GtkDialog isn't quite flexible enough */
    fe->cfgbox = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    content_box = GTK_BOX(gtk_vbox_new(false, 8));
    g_object_set(G_OBJECT(content_box), "margin", 8, (const char *)NULL);
    gtk_widget_show(GTK_WIDGET(content_box));
    gtk_container_add(GTK_CONTAINER(fe->cfgbox), GTK_WIDGET(content_box));
    button_box = GTK_BOX(gtk_hbox_new(false, 8));
    gtk_widget_show(GTK_WIDGET(button_box));
    gtk_box_pack_end(content_box, GTK_WIDGET(button_box), false, false, 0);
    {
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_show(sep);
        gtk_box_pack_end(content_box, sep, false, false, 0);
    }
#else
    fe->cfgbox = gtk_dialog_new();
    content_box = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(fe->cfgbox)));
    button_box = GTK_BOX(gtk_dialog_get_action_area(GTK_DIALOG(fe->cfgbox)));
#endif
    gtk_window_set_title(GTK_WINDOW(fe->cfgbox), title);
    sfree(title);

    w = gtk_button_new_with_our_label(LABEL_CANCEL);
    gtk_box_pack_end(button_box, w, false, false, 0);
    gtk_widget_show(w);
    g_signal_connect(G_OBJECT(w), "clicked",
                     G_CALLBACK(config_cancel_button_clicked), fe);
    cancel = w;

    w = gtk_button_new_with_our_label(LABEL_OK);
    gtk_box_pack_end(button_box, w, false, false, 0);
    gtk_widget_show(w);
    gtk_widget_set_can_default(w, true);
    gtk_window_set_default(GTK_WINDOW(fe->cfgbox), w);
    g_signal_connect(G_OBJECT(w), "clicked",
                     G_CALLBACK(config_ok_button_clicked), fe);

#if GTK_CHECK_VERSION(3,0,0)
    table = gtk_grid_new();
#else
    table = gtk_table_new(1, 2, false);
#endif
    y = 0;
    gtk_box_pack_start(content_box, table, false, false, 0);
    gtk_widget_show(table);

    for (i = fe->cfg; i->type != C_END; i++) {
#if !GTK_CHECK_VERSION(3,0,0)
	gtk_table_resize(GTK_TABLE(table), y+1, 2);
#endif

	switch (i->type) {
	  case C_STRING:
	    /*
	     * Edit box with a label beside it.
	     */

	    w = gtk_label_new(i->name);
	    align_label(GTK_LABEL(w), 0.0, 0.5);
#if GTK_CHECK_VERSION(3,0,0)
            gtk_grid_attach(GTK_GRID(table), w, 0, y, 1, 1);
#else
	    gtk_table_attach(GTK_TABLE(table), w, 0, 1, y, y+1,
			     GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     3, 3);
#endif
	    gtk_widget_show(w);

	    w = gtk_entry_new();
#if GTK_CHECK_VERSION(3,0,0)
            gtk_grid_attach(GTK_GRID(table), w, 1, y, 1, 1);
            g_object_set(G_OBJECT(w), "hexpand", true, (const char *)NULL);
#else
	    gtk_table_attach(GTK_TABLE(table), w, 1, 2, y, y+1,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     3, 3);
#endif
	    gtk_entry_set_text(GTK_ENTRY(w), i->u.string.sval);
	    g_signal_connect(G_OBJECT(w), "changed",
                             G_CALLBACK(editbox_changed), i);
	    g_signal_connect(G_OBJECT(w), "key_press_event",
                             G_CALLBACK(editbox_key), NULL);
	    gtk_widget_show(w);

	    break;

	  case C_BOOLEAN:
	    /*
	     * Simple checkbox.
	     */
            w = gtk_check_button_new_with_label(i->name);
	    g_signal_connect(G_OBJECT(w), "toggled",
                             G_CALLBACK(button_toggled), i);
#if GTK_CHECK_VERSION(3,0,0)
            gtk_grid_attach(GTK_GRID(table), w, 0, y, 2, 1);
            g_object_set(G_OBJECT(w), "hexpand", true, (const char *)NULL);
#else
	    gtk_table_attach(GTK_TABLE(table), w, 0, 2, y, y+1,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     3, 3);
#endif
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),
                                         i->u.boolean.bval);
	    gtk_widget_show(w);
	    break;

	  case C_CHOICES:
	    /*
	     * Drop-down list (GtkComboBox).
	     */

	    w = gtk_label_new(i->name);
	    align_label(GTK_LABEL(w), 0.0, 0.5);
#if GTK_CHECK_VERSION(3,0,0)
            gtk_grid_attach(GTK_GRID(table), w, 0, y, 1, 1);
#else
	    gtk_table_attach(GTK_TABLE(table), w, 0, 1, y, y+1,
			     GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL ,
			     3, 3);
#endif
	    gtk_widget_show(w);

            {
		int c;
		const char *p, *q;
                char *name;
                GtkListStore *model;
		GtkCellRenderer *cr;
                GtkTreeIter iter;

                model = gtk_list_store_new(1, G_TYPE_STRING);

		c = *i->u.choices.choicenames;
		p = i->u.choices.choicenames+1;

		while (*p) {
		    q = p;
		    while (*q && *q != c)
			q++;

		    name = snewn(q-p+1, char);
		    strncpy(name, p, q-p);
		    name[q-p] = '\0';

		    if (*q) q++;       /* eat delimiter */

                    gtk_list_store_append(model, &iter);
                    gtk_list_store_set(model, &iter, 0, name, -1);

		    p = q;
		}

                w = gtk_combo_box_new_with_model(GTK_TREE_MODEL(model));

		gtk_combo_box_set_active(GTK_COMBO_BOX(w),
                                         i->u.choices.selected);

		cr = gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(w), cr, true);
		gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(w), cr,
					       "text", 0, NULL);

		g_signal_connect(G_OBJECT(w), "changed",
				 G_CALLBACK(droplist_sel), i);
            }

#if GTK_CHECK_VERSION(3,0,0)
            gtk_grid_attach(GTK_GRID(table), w, 1, y, 1, 1);
            g_object_set(G_OBJECT(w), "hexpand", true, (const char *)NULL);
#else
	    gtk_table_attach(GTK_TABLE(table), w, 1, 2, y, y+1,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     3, 3);
#endif
	    gtk_widget_show(w);
	    break;
	}

	y++;
    }

    g_signal_connect(G_OBJECT(fe->cfgbox), "destroy",
                     G_CALLBACK(window_destroy), NULL);
    g_signal_connect(G_OBJECT(fe->cfgbox), "key_press_event",
                     G_CALLBACK(win_key_press), cancel);
    gtk_window_set_modal(GTK_WINDOW(fe->cfgbox), true);
    gtk_window_set_transient_for(GTK_WINDOW(fe->cfgbox),
				 GTK_WINDOW(fe->window));
    /* set_transient_window_pos(fe->window, fe->cfgbox); */
    gtk_widget_show(fe->cfgbox);
    gtk_main();

    free_cfg(fe->cfg);

    return fe->cfgret;
}

static void menu_key_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    int key = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem),
                                                "user-data"));
    if (!midend_process_key(fe->me, 0, 0, key, NULL))
	gtk_widget_destroy(fe->window);
}

static void get_size(frontend *fe, int *px, int *py)
{
    int x, y;

    /*
     * Currently I don't want to make the GTK port scale large
     * puzzles to fit on the screen. This is because X does permit
     * extremely large windows and many window managers provide a
     * means of navigating round them, and the users I consulted
     * before deciding said that they'd rather have enormous puzzle
     * windows spanning multiple screen pages than have them
     * shrunk. I could change my mind later or introduce
     * configurability; this would be the place to do so, by
     * replacing the initial values of x and y with the screen
     * dimensions.
     */
    x = INT_MAX;
    y = INT_MAX;
    midend_size(fe->me, &x, &y, false, 1.0);
    *px = x;
    *py = y;
}

#if !GTK_CHECK_VERSION(2,0,0)
#define gtk_window_resize(win, x, y) \
	gdk_window_resize(GTK_WIDGET(win)->window, x, y)
#endif

/*
 * Called when any other code in this file has changed the
 * selected game parameters.
 */
static void changed_preset(frontend *fe)
{
    int n = midend_which_preset(fe->me);

    fe->preset_threaded = true;
    if (n < 0 && fe->preset_custom) {
	gtk_check_menu_item_set_active(
	    GTK_CHECK_MENU_ITEM(fe->preset_custom),
	    true);
    } else {
	GSList *gs = fe->preset_radio;
        GSList *found = NULL;

        for (gs = fe->preset_radio; gs; gs = gs->next) {
            struct preset_menu_entry *entry =
                (struct preset_menu_entry *)g_object_get_data(
                    G_OBJECT(gs->data), "user-data");
            if (!entry || entry->id != n)
                gtk_check_menu_item_set_active(
                    GTK_CHECK_MENU_ITEM(gs->data), false);
            else
                found = gs;
        }
        if (found)
            gtk_check_menu_item_set_active(
                GTK_CHECK_MENU_ITEM(found->data), true);
    }
    fe->preset_threaded = false;

    /*
     * Update the greying on the Copy menu option.
     */
    if (fe->copy_menu_item) {
        bool enabled = midend_can_format_as_text_now(fe->me);
	gtk_widget_set_sensitive(fe->copy_menu_item, enabled);
    }
}

#if !GTK_CHECK_VERSION(3,0,0)
static bool not_size_allocated_yet(GtkWidget *w)
{
    /*
     * This function tests whether a widget has not yet taken up space
     * on the screen which it will occupy in future. (Therefore, it
     * returns true only if the widget does exist but does not have a
     * size allocation. A null widget is already taking up all the
     * space it ever will.)
     */
    if (!w)
        return false;        /* nonexistent widgets aren't a problem */

#if GTK_CHECK_VERSION(2,18,0)  /* skip if no gtk_widget_get_allocation */
    {
        GtkAllocation a;
        gtk_widget_get_allocation(w, &a);
        if (a.height == 0 || a.width == 0)
            return true;       /* widget exists but has no size yet */
    }
#endif

    return false;
}

static void try_shrink_drawing_area(frontend *fe)
{
    if (fe->drawing_area_shrink_pending &&
        (!fe->menubar_is_local || !not_size_allocated_yet(fe->menubar)) &&
        !not_size_allocated_yet(fe->statusbar)) {
        /*
         * In order to permit the user to resize the window smaller as
         * well as bigger, we call this function after the window size
         * has ended up where we want it. This shouldn't shrink the
         * window immediately; it just arranges that the next time the
         * user tries to shrink it, they can.
         *
         * However, at puzzle creation time, we defer the first of
         * these operations until after the menu bar and status bar
         * are actually visible. On Ubuntu 12.04 I've found that these
         * can take a while to be displayed, and that it's a mistake
         * to reduce the drawing area's size allocation before they've
         * turned up or else the drawing area makes room for them by
         * shrinking to less than the size we intended.
         */
        gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), 1, 1);
        fe->drawing_area_shrink_pending = false;
    }
}
#endif /* !GTK_CHECK_VERSION(3,0,0) */

static gint configure_window(GtkWidget *widget,
                             GdkEventConfigure *event, gpointer data)
{
#if !GTK_CHECK_VERSION(3,0,0)
    /*
     * When the main puzzle window changes size, it might be because
     * the menu bar or status bar has turned up after starting off
     * absent, in which case we should have another go at enacting a
     * pending shrink of the drawing area.
     */
    frontend *fe = (frontend *)data;
    try_shrink_drawing_area(fe);
#endif
    return false;
}

#if GTK_CHECK_VERSION(3,0,0)
static int window_extra_height(frontend *fe)
{
    int ret = 0;
    if (fe->menubar) {
        GtkRequisition req;
        gtk_widget_get_preferred_size(fe->menubar, &req, NULL);
        ret += req.height;
    }
    if (fe->statusbar) {
        GtkRequisition req;
        gtk_widget_get_preferred_size(fe->statusbar, &req, NULL);
        ret += req.height;
    }
    return ret;
}
#endif

static void resize_fe(frontend *fe)
{
    int x, y;

    get_size(fe, &x, &y);

#if GTK_CHECK_VERSION(3,0,0)
    gtk_window_resize(GTK_WINDOW(fe->window), x, y + window_extra_height(fe));
    fe->awaiting_resize_ack = true;
#else
    fe->drawing_area_shrink_pending = false;
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    {
        GtkRequisition req;
        gtk_widget_size_request(GTK_WIDGET(fe->window), &req);
        gtk_window_resize(GTK_WINDOW(fe->window), req.width, req.height);
    }
    fe->drawing_area_shrink_pending = true;
    try_shrink_drawing_area(fe);
#endif
}

static void menu_preset_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    struct preset_menu_entry *entry =
        (struct preset_menu_entry *)g_object_get_data(
            G_OBJECT(menuitem), "user-data");

    if (fe->preset_threaded ||
	(GTK_IS_CHECK_MENU_ITEM(menuitem) &&
	 !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))))
	return;
    midend_set_params(fe->me, entry->params);
    midend_new_game(fe->me);
    changed_preset(fe);
    resize_fe(fe);
    midend_redraw(fe->me);
}

static GdkAtom compound_text_atom, utf8_string_atom;
static bool paste_initialised = false;

static void set_selection(frontend *fe, GdkAtom selection)
{
    if (!paste_initialised) {
	compound_text_atom = gdk_atom_intern("COMPOUND_TEXT", false);
	utf8_string_atom = gdk_atom_intern("UTF8_STRING", false);
	paste_initialised = true;
    }

    /*
     * For this simple application we can safely assume that the
     * data passed to this function is pure ASCII, which means we
     * can return precisely the same stuff for types STRING,
     * COMPOUND_TEXT or UTF8_STRING.
     */

    if (gtk_selection_owner_set(fe->window, selection, CurrentTime)) {
	gtk_selection_clear_targets(fe->window, selection);
	gtk_selection_add_target(fe->window, selection,
				 GDK_SELECTION_TYPE_STRING, 1);
	gtk_selection_add_target(fe->window, selection, compound_text_atom, 1);
	gtk_selection_add_target(fe->window, selection, utf8_string_atom, 1);
    }
}

static void write_clip(frontend *fe, char *data)
{
    if (fe->paste_data)
	sfree(fe->paste_data);

    fe->paste_data = data;
    fe->paste_data_len = strlen(data);

    set_selection(fe, GDK_SELECTION_PRIMARY);
    set_selection(fe, GDK_SELECTION_CLIPBOARD);
}

static void selection_get(GtkWidget *widget, GtkSelectionData *seldata,
                          guint info, guint time_stamp, gpointer data)
{
    frontend *fe = (frontend *)data;
    gtk_selection_data_set(seldata, gtk_selection_data_get_target(seldata), 8,
			   fe->paste_data, fe->paste_data_len);
}

static gint selection_clear(GtkWidget *widget, GdkEventSelection *seldata,
                            gpointer data)
{
    frontend *fe = (frontend *)data;

    if (fe->paste_data)
	sfree(fe->paste_data);
    fe->paste_data = NULL;
    fe->paste_data_len = 0;
    return true;
}

static void menu_copy_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    char *text;

    text = midend_text_format(fe->me);

    if (text) {
	write_clip(fe, text);
    } else {
        gdk_display_beep(gdk_display_get_default());
    }
}

#ifdef OLD_FILESEL

static void filesel_ok(GtkButton *button, gpointer data)
{
    frontend *fe = (frontend *)data;

    gpointer filesel = g_object_get_data(G_OBJECT(button), "user-data");

    const char *name =
        gtk_file_selection_get_filename(GTK_FILE_SELECTION(filesel));

    fe->filesel_name = dupstr(name);
}

static char *file_selector(frontend *fe, const char *title, int save)
{
    GtkWidget *filesel =
        gtk_file_selection_new(title);

    fe->filesel_name = NULL;

    gtk_window_set_modal(GTK_WINDOW(filesel), true);
    g_object_set_data
        (G_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "user-data",
         (gpointer)filesel);
    g_signal_connect
        (G_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "clicked",
         G_CALLBACK(filesel_ok), fe);
    g_signal_connect_swapped
        (G_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "clicked",
         G_CALLBACK(gtk_widget_destroy), (gpointer)filesel);
    g_signal_connect_object
        (G_OBJECT(GTK_FILE_SELECTION(filesel)->cancel_button), "clicked",
         G_CALLBACK(gtk_widget_destroy), (gpointer)filesel);
    g_signal_connect(G_OBJECT(filesel), "destroy",
                     G_CALLBACK(window_destroy), NULL);
    gtk_widget_show(filesel);
    gtk_window_set_transient_for(GTK_WINDOW(filesel), GTK_WINDOW(fe->window));
    gtk_main();

    return fe->filesel_name;
}

#else

static char *file_selector(frontend *fe, const char *title, bool save)
{
    char *filesel_name = NULL;

    GtkWidget *filesel =
        gtk_file_chooser_dialog_new(title,
				    GTK_WINDOW(fe->window),
				    save ? GTK_FILE_CHOOSER_ACTION_SAVE :
				    GTK_FILE_CHOOSER_ACTION_OPEN,
				    LABEL_CANCEL, GTK_RESPONSE_CANCEL,
				    save ? LABEL_SAVE : LABEL_OPEN,
				    GTK_RESPONSE_ACCEPT,
				    NULL);

    if (gtk_dialog_run(GTK_DIALOG(filesel)) == GTK_RESPONSE_ACCEPT) {
        char *name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(filesel));
        filesel_name = dupstr(name);
        g_free(name);
    }

    gtk_widget_destroy(filesel);

    return filesel_name;
}

#endif

#ifdef USE_PRINTING
static GObject *create_print_widget(GtkPrintOperation *print, gpointer data)
{
    GtkLabel *count_label, *width_label, *height_label,
        *scale_llabel, *scale_rlabel;
    GtkBox *scale_hbox;
    GtkWidget *grid;
    frontend *fe = (frontend *)data;

    fe->printcount_spin_button =
        GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 999, 1));
    gtk_spin_button_set_numeric(fe->printcount_spin_button, true);
    gtk_spin_button_set_snap_to_ticks(fe->printcount_spin_button, true);
    fe->printw_spin_button =
        GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 99, 1));
    gtk_spin_button_set_numeric(fe->printw_spin_button, true);
    gtk_spin_button_set_snap_to_ticks(fe->printw_spin_button, true);
    fe->printh_spin_button =
        GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 99, 1));
    gtk_spin_button_set_numeric(fe->printh_spin_button, true);
    gtk_spin_button_set_snap_to_ticks(fe->printh_spin_button, true);
    fe->printscale_spin_button =
        GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(1, 1000, 1));
    gtk_spin_button_set_digits(fe->printscale_spin_button, 1);
    gtk_spin_button_set_numeric(fe->printscale_spin_button, true);
    if (thegame.can_solve) {
        fe->soln_check_button =
            GTK_CHECK_BUTTON(
                gtk_check_button_new_with_label("Print solutions"));
    }
    if (thegame.can_print_in_colour) {
        fe->colour_check_button =
            GTK_CHECK_BUTTON(
                gtk_check_button_new_with_label("Print in color"));
    }

    /* Set defaults to what was selected last time. */
    gtk_spin_button_set_value(fe->printcount_spin_button,
                              (gdouble)fe->printcount);
    gtk_spin_button_set_value(fe->printw_spin_button,
                              (gdouble)fe->printw);
    gtk_spin_button_set_value(fe->printh_spin_button,
                              (gdouble)fe->printh);
    gtk_spin_button_set_value(fe->printscale_spin_button,
                              (gdouble)fe->printscale);
    if (thegame.can_solve) {
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(fe->soln_check_button), fe->printsolns);
    }
    if (thegame.can_print_in_colour) {
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(fe->colour_check_button), fe->printcolour);
    }

    count_label = GTK_LABEL(gtk_label_new("Puzzles to print:"));
    width_label = GTK_LABEL(gtk_label_new("Puzzles across:"));
    height_label = GTK_LABEL(gtk_label_new("Puzzles down:"));
    scale_llabel = GTK_LABEL(gtk_label_new("Puzzle scale:"));
    scale_rlabel = GTK_LABEL(gtk_label_new("%"));
#if GTK_CHECK_VERSION(3,0,0)
    gtk_widget_set_halign(GTK_WIDGET(count_label), GTK_ALIGN_START);
    gtk_widget_set_halign(GTK_WIDGET(width_label), GTK_ALIGN_START);
    gtk_widget_set_halign(GTK_WIDGET(height_label), GTK_ALIGN_START);
    gtk_widget_set_halign(GTK_WIDGET(scale_llabel), GTK_ALIGN_START);
#else
    gtk_misc_set_alignment(GTK_MISC(count_label), 0, 0);
    gtk_misc_set_alignment(GTK_MISC(width_label), 0, 0);
    gtk_misc_set_alignment(GTK_MISC(height_label), 0, 0);
    gtk_misc_set_alignment(GTK_MISC(scale_llabel), 0, 0);
#endif

    scale_hbox = GTK_BOX(gtk_hbox_new(false, 6));
    gtk_box_pack_start(scale_hbox, GTK_WIDGET(fe->printscale_spin_button),
                       false, false, 0);
    gtk_box_pack_start(scale_hbox, GTK_WIDGET(scale_rlabel),
                       false, false, 0);

#if GTK_CHECK_VERSION(3,0,0)
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 18);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(count_label), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(width_label), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(height_label), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(scale_llabel), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(fe->printcount_spin_button),
		    1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(fe->printw_spin_button),
		    1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(fe->printh_spin_button),
		    1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(scale_hbox), 1, 3, 1, 1);
    if (thegame.can_solve) {
        gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(fe->soln_check_button),
                        0, 4, 1, 1);
    }
    if (thegame.can_print_in_colour) {
        gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(fe->colour_check_button),
                        thegame.can_solve, 4, 1, 1);
    }
#else
    grid = gtk_table_new((thegame.can_solve || thegame.can_print_in_colour) ?
                         5 : 4, 2, false);
    gtk_table_set_col_spacings(GTK_TABLE(grid), 18);
    gtk_table_set_row_spacings(GTK_TABLE(grid), 18);
    gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(count_label), 0, 1, 0, 1,
                     GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(width_label), 0, 1, 1, 2,
                     GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(height_label), 0, 1, 2, 3,
                     GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(scale_llabel), 0, 1, 3, 4,
                     GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(fe->printcount_spin_button),
                     1, 2, 0, 1,
                     GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(fe->printw_spin_button),
                     1, 2, 1, 2,
                     GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(fe->printh_spin_button),
                     1, 2, 2, 3,
                     GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(scale_hbox), 1, 2, 3, 4,
                     GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    if (thegame.can_solve) {
        gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(fe->soln_check_button),
                         0, 1, 4, 5,
                         GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    }
    if (thegame.can_print_in_colour) {
        gtk_table_attach(GTK_TABLE(grid), GTK_WIDGET(fe->colour_check_button),
                         thegame.can_solve, thegame.can_solve + 1, 4, 5,
                         GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
    }
#endif
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);

    gtk_widget_show_all(grid);

    return G_OBJECT(grid);
}

static void apply_print_widget(GtkPrintOperation *print,
                               GtkWidget *widget, gpointer data)
{
    frontend *fe = (frontend *)data;

    /* We ignore `widget' because it is easier and faster to store the
       widgets we need in `fe' then to get the children of `widget'. */
    fe->printcount =
        gtk_spin_button_get_value_as_int(fe->printcount_spin_button);
    fe->printw = gtk_spin_button_get_value_as_int(fe->printw_spin_button);
    fe->printh = gtk_spin_button_get_value_as_int(fe->printh_spin_button);
    fe->printscale = gtk_spin_button_get_value(fe->printscale_spin_button);
    if (thegame.can_solve) {
        fe->printsolns =
            gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(fe->soln_check_button));
    }
    if (thegame.can_print_in_colour) {
        fe->printcolour =
            gtk_toggle_button_get_active(
                GTK_TOGGLE_BUTTON(fe->colour_check_button));
    }
}

static void print_begin(GtkPrintOperation *printop,
                        GtkPrintContext *context, gpointer data)
{
    frontend *fe = (frontend *)data;
    midend *nme = NULL;  /* non-interactive midend for bulk puzzle generation */
    int i;

    fe->printcontext = context;
    fe->cr = gtk_print_context_get_cairo_context(context);

    /*
     * Create our document structure and fill it up with puzzles.
     */
    fe->doc = document_new(fe->printw, fe->printh, fe->printscale / 100.0F);

    for (i = 0; i < fe->printcount; i++) {
        const char *err;

        if (i == 0) {
            err = midend_print_puzzle(fe->me, fe->doc, fe->printsolns);
        } else {
	    if (!nme) {
		game_params *params;

		nme = midend_new(NULL, &thegame, NULL, NULL);

		/*
		 * Set the non-interactive mid-end to have the same
		 * parameters as the standard one.
		 */
		params = midend_get_params(fe->me);
		midend_set_params(nme, params);
		thegame.free_params(params);
	    }

            midend_new_game(nme);
            err = midend_print_puzzle(nme, fe->doc, fe->printsolns);
        }

        if (err) {
            error_box(fe->window, err);
            return;
        }
    }

    if (nme)
        midend_free(nme);

    /* Begin the document. */
    document_begin(fe->doc, fe->print_dr);
}

static void draw_page(GtkPrintOperation *printop,
                      GtkPrintContext *context,
                      gint page_nr, gpointer data)
{
    frontend *fe = (frontend *)data;
    document_print_page(fe->doc, fe->print_dr, page_nr);
}

static void print_end(GtkPrintOperation *printop,
                      GtkPrintContext *context, gpointer data)
{
    frontend *fe = (frontend *)data;

    /* End and free the document. */
    document_end(fe->doc, fe->print_dr);
    document_free(fe->doc);
    fe->doc = NULL;
}

static void print_dialog(frontend *fe)
{
    GError *error;
    static GtkPrintSettings *settings = NULL;
    static GtkPageSetup *page_setup = NULL;
#ifndef USE_EMBED_PAGE_SETUP
    GtkPageSetup *new_page_setup;
#endif

    fe->printop = gtk_print_operation_new();
    gtk_print_operation_set_use_full_page(fe->printop, true);
    gtk_print_operation_set_custom_tab_label(fe->printop, "Puzzle Settings");
    g_signal_connect(fe->printop, "create-custom-widget",
                     G_CALLBACK(create_print_widget), fe);
    g_signal_connect(fe->printop, "custom-widget-apply",
                     G_CALLBACK(apply_print_widget), fe);
    g_signal_connect(fe->printop, "begin-print", G_CALLBACK(print_begin), fe);
    g_signal_connect(fe->printop, "draw-page", G_CALLBACK(draw_page), fe);
    g_signal_connect(fe->printop, "end-print", G_CALLBACK(print_end), fe);
#ifdef USE_EMBED_PAGE_SETUP
    gtk_print_operation_set_embed_page_setup(fe->printop, true);
#else
    if (page_setup == NULL) {
        page_setup =
            g_object_ref(
                gtk_print_operation_get_default_page_setup(fe->printop));
    }
    if (settings == NULL) {
        settings =
            g_object_ref(gtk_print_operation_get_print_settings(fe->printop));
    }
    new_page_setup = gtk_print_run_page_setup_dialog(GTK_WINDOW(fe->window),
                                                     page_setup, settings);
    g_object_unref(page_setup);
    page_setup = new_page_setup;
    gtk_print_operation_set_default_page_setup(fe->printop, page_setup);
#endif

    if (settings != NULL)
        gtk_print_operation_set_print_settings(fe->printop, settings);
    if (page_setup != NULL)
        gtk_print_operation_set_default_page_setup(fe->printop, page_setup);

    switch (gtk_print_operation_run(fe->printop,
                                    GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                    GTK_WINDOW(fe->window), &error)) {
    case GTK_PRINT_OPERATION_RESULT_ERROR:
        error_box(fe->window, error->message);
        g_error_free(error);
        break;
    case GTK_PRINT_OPERATION_RESULT_APPLY:
        if (settings != NULL)
            g_object_unref(settings);
        settings =
            g_object_ref(gtk_print_operation_get_print_settings(fe->printop));
#ifdef USE_EMBED_PAGE_SETUP
        if (page_setup != NULL)
            g_object_unref(page_setup);
        page_setup =
            g_object_ref(
                gtk_print_operation_get_default_page_setup(fe->printop));
#endif
        break;
    default:
        /* Don't error out on -Werror=switch. */
        break;
    }

    g_object_unref(fe->printop);
    fe->printop = NULL;
    fe->printcontext = NULL;
}
#endif /* USE_PRINTING */

struct savefile_write_ctx {
    FILE *fp;
    int error;
};

static void savefile_write(void *wctx, const void *buf, int len)
{
    struct savefile_write_ctx *ctx = (struct savefile_write_ctx *)wctx;
    if (fwrite(buf, 1, len, ctx->fp) < len)
	ctx->error = errno;
}

static bool savefile_read(void *wctx, void *buf, int len)
{
    FILE *fp = (FILE *)wctx;
    int ret;

    ret = fread(buf, 1, len, fp);
    return (ret == len);
}

static void menu_save_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    char *name;

    name = file_selector(fe, "Enter name of game file to save", true);

    if (name) {
        FILE *fp;

	if ((fp = fopen(name, "r")) != NULL) {
	    char buf[256 + FILENAME_MAX];
	    fclose(fp);
	    /* file exists */

	    sprintf(buf, "Are you sure you want to overwrite the"
		    " file \"%.*s\"?",
		    FILENAME_MAX, name);
	    if (!message_box(fe->window, "Question", buf, true, MB_YESNO))
                goto free_and_return;
	}

	fp = fopen(name, "w");

        if (!fp) {
            error_box(fe->window, "Unable to open save file");
            goto free_and_return;
        }

	{
	    struct savefile_write_ctx ctx;
	    ctx.fp = fp;
	    ctx.error = 0;
	    midend_serialise(fe->me, savefile_write, &ctx);
	    fclose(fp);
	    if (ctx.error) {
		char boxmsg[512];
		sprintf(boxmsg, "Error writing save file: %.400s",
			strerror(ctx.error));
		error_box(fe->window, boxmsg);
		goto free_and_return;
	    }
	}
    free_and_return:
        sfree(name);
    }
}

static void menu_load_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    char *name;
    const char *err;

    name = file_selector(fe, "Enter name of saved game file to load", false);

    if (name) {
        FILE *fp = fopen(name, "r");
        sfree(name);

        if (!fp) {
            error_box(fe->window, "Unable to open saved game file");
            return;
        }

        err = midend_deserialise(fe->me, savefile_read, fp);

        fclose(fp);

        if (err) {
            error_box(fe->window, err);
            return;
        }

	changed_preset(fe);
        resize_fe(fe);
        midend_redraw(fe->me);
    }
}

#ifdef USE_PRINTING
static void menu_print_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;

    print_dialog(fe);
}
#endif

static void menu_solve_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    const char *msg;

    msg = midend_solve(fe->me);

    if (msg)
	error_box(fe->window, msg);
}

static void menu_restart_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;

    midend_restart_game(fe->me);
}

static void menu_config_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    int which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem),
                                                  "user-data"));

    if (fe->preset_threaded ||
	(GTK_IS_CHECK_MENU_ITEM(menuitem) &&
	 !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))))
	return;
    changed_preset(fe); 		/* Put the old preset back! */
    if (!get_config(fe, which))
	return;

    midend_new_game(fe->me);
    resize_fe(fe);
    midend_redraw(fe->me);
}

#ifndef HELP_BROWSER_PATH
#define HELP_BROWSER_PATH "xdg-open:sensible-browser:$BROWSER"
#endif

static bool try_show_help(const char *browser, const char *help_name)
{
    const char *argv[3] = { browser, help_name, NULL };

    return g_spawn_async(NULL, (char **)argv, NULL,
			 G_SPAWN_SEARCH_PATH,
			 NULL, NULL, NULL, NULL);
}

static void show_help(frontend *fe, const char *topic)
{
    char *path = dupstr(HELP_BROWSER_PATH);
    char *path_entry;
    char *help_name;
    size_t help_name_size;
    bool succeeded = true;

    help_name_size = strlen(HELP_DIR) + 4 + strlen(topic) + 6;
    help_name = snewn(help_name_size, char);
    sprintf(help_name, "%s/en/%s.html",
	    HELP_DIR, topic);

    if (access(help_name, R_OK)) {
	error_box(fe->window, "Help file is not installed");
	sfree(path);
	sfree(help_name);
	return;
    }

    path_entry = path;
    for (;;) {
	size_t len;
	bool last;

	len = strcspn(path_entry, ":");
	last = path_entry[len] == 0;
	path_entry[len] = 0;

	if (path_entry[0] == '$') {
	    const char *command = getenv(path_entry + 1);

	    if (command)
		succeeded = try_show_help(command, help_name);
	} else {
	    succeeded = try_show_help(path_entry, help_name);
	}

	if (last || succeeded)
	    break;
	path_entry += len + 1;
    }

    if (!succeeded)
	error_box(fe->window, "Failed to start a help browser");
    sfree(path);
    sfree(help_name);
}

static void menu_help_contents_event(GtkMenuItem *menuitem, gpointer data)
{
    show_help((frontend *)data, "index");
}

static void menu_help_specific_event(GtkMenuItem *menuitem, gpointer data)
{
    show_help((frontend *)data, thegame.htmlhelp_topic);
}

static void menu_about_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;

#if GTK_CHECK_VERSION(3,0,0)
# define ABOUT_PARAMS                                               \
    "program-name", thegame.name,                                   \
    "version", ver,                                                 \
    "comments", "Part of Simon Tatham's Portable Puzzle Collection"

    if (n_xpm_icons) {
        GdkPixbuf *icon = gdk_pixbuf_new_from_xpm_data
            ((const gchar **)xpm_icons[0]);

        gtk_show_about_dialog
            (GTK_WINDOW(fe->window),
             ABOUT_PARAMS,
             "logo", icon,
             (const gchar *)NULL);
        g_object_unref(G_OBJECT(icon));
    }
    else {
        gtk_show_about_dialog
            (GTK_WINDOW(fe->window),
             ABOUT_PARAMS,
             (const gchar *)NULL);
    }
#else
    char titlebuf[256];
    char textbuf[1024];

    sprintf(titlebuf, "About %.200s", thegame.name);
    sprintf(textbuf,
	    "%.200s\n\n"
	    "from Simon Tatham's Portable Puzzle Collection\n\n"
	    "%.500s", thegame.name, ver);

    message_box(fe->window, titlebuf, textbuf, true, MB_OK);
#endif
}

static GtkWidget *add_menu_ui_item(
    frontend *fe, GtkContainer *cont, const char *text, int action,
    int accel_key, int accel_keyqual)
{
    GtkWidget *menuitem = gtk_menu_item_new_with_label(text);
    gtk_container_add(cont, menuitem);
    g_object_set_data(G_OBJECT(menuitem), "user-data",
                      GINT_TO_POINTER(action));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(menu_key_event), fe);

    if (accel_key) {
        /*
         * Display a keyboard accelerator alongside this menu item.
         * Actually this won't be processed via the usual GTK
         * accelerator system, because we add it to a dummy
         * accelerator group which is never actually activated on the
         * main window; this permits back ends to override special
         * keys like 'n' and 'r' and 'u' in some UI states. So
         * whatever keystroke we display here will still go to
         * key_event and be handled in the normal way.
         */
        gtk_widget_add_accelerator(menuitem,
                                   "activate", fe->dummy_accelgroup,
                                   accel_key, accel_keyqual,
                                   GTK_ACCEL_VISIBLE | GTK_ACCEL_LOCKED);
    }

    gtk_widget_show(menuitem);
    return menuitem;
}

static void add_menu_separator(GtkContainer *cont)
{
    GtkWidget *menuitem = gtk_menu_item_new();
    gtk_container_add(cont, menuitem);
    gtk_widget_show(menuitem);
}

static void populate_gtk_preset_menu(frontend *fe, struct preset_menu *menu,
                                     GtkWidget *gtkmenu)
{
    int i;

    for (i = 0; i < menu->n_entries; i++) {
        struct preset_menu_entry *entry = &menu->entries[i];
        GtkWidget *menuitem;

        if (entry->params) {
            menuitem = gtk_radio_menu_item_new_with_label(
                fe->preset_radio, entry->title);
            fe->preset_radio = gtk_radio_menu_item_get_group(
                GTK_RADIO_MENU_ITEM(menuitem));
            g_object_set_data(G_OBJECT(menuitem), "user-data", entry);
            g_signal_connect(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(menu_preset_event), fe);
        } else {
            GtkWidget *submenu;
            menuitem = gtk_menu_item_new_with_label(entry->title);
            submenu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
            populate_gtk_preset_menu(fe, entry->submenu, submenu);
        }

        gtk_container_add(GTK_CONTAINER(gtkmenu), menuitem);
        gtk_widget_show(menuitem);
    }
}

enum { ARG_EITHER, ARG_SAVE, ARG_ID }; /* for argtype */

static frontend *new_window(
    char *arg, int argtype, char **error, bool headless)
{
    frontend *fe;
#ifdef USE_PRINTING
    frontend *print_fe = NULL;
#endif
    GtkBox *vbox, *hbox;
    GtkWidget *menu, *menuitem;
    GList *iconlist;
    int x, y, n;
    char errbuf[1024];
    struct preset_menu *preset_menu;

    fe = snew(frontend);
    memset(fe, 0, sizeof(frontend));

#ifndef USE_CAIRO
    if (headless) {
        fprintf(stderr, "headless mode not supported for non-Cairo drawing\n");
        exit(1);
    }
#else
    fe->headless = headless;
    fe->ps = 1; /* in headless mode, configure_area won't have set this */
#endif

    fe->timer_active = false;
    fe->timer_id = -1;

    fe->me = midend_new(fe, &thegame, &gtk_drawing, fe);

    fe->dr_api = &internal_drawing;

#ifdef USE_PRINTING
    if (thegame.can_print) {
        print_fe = snew(frontend);
        memset(print_fe, 0, sizeof(frontend));

        /* Defaults */
        print_fe->printcount = print_fe->printw = print_fe->printh = 1;
        print_fe->printscale = 100;
        print_fe->printsolns = false;
        print_fe->printcolour = thegame.can_print_in_colour;

        /*
         * We need to use the same midend as the main frontend because
         * we need midend_print_puzzle() to be able to print the
         * current puzzle.
         */
        print_fe->me = fe->me;

        print_fe->print_dr = drawing_new(&gtk_drawing, print_fe->me, print_fe);

        print_fe->dr_api = &internal_printing;
    }
#endif

    if (arg) {
	const char *err;
	FILE *fp;

	errbuf[0] = '\0';

	switch (argtype) {
	  case ARG_ID:
	    err = midend_game_id(fe->me, arg);
	    if (!err)
		midend_new_game(fe->me);
	    else
		sprintf(errbuf, "Invalid game ID: %.800s", err);
	    break;
	  case ARG_SAVE:
	    fp = fopen(arg, "r");
	    if (!fp) {
		sprintf(errbuf, "Error opening file: %.800s", strerror(errno));
	    } else {
		err = midend_deserialise(fe->me, savefile_read, fp);
                if (err)
                    sprintf(errbuf, "Invalid save file: %.800s", err);
                fclose(fp);
	    }
	    break;
	  default /*case ARG_EITHER*/:
	    /*
	     * First try treating the argument as a game ID.
	     */
	    err = midend_game_id(fe->me, arg);
	    if (!err) {
		/*
		 * It's a valid game ID.
		 */
		midend_new_game(fe->me);
	    } else {
		FILE *fp = fopen(arg, "r");
		if (!fp) {
		    sprintf(errbuf, "Supplied argument is neither a game ID (%.400s)"
			    " nor a save file (%.400s)", err, strerror(errno));
		} else {
		    err = midend_deserialise(fe->me, savefile_read, fp);
		    if (err)
			sprintf(errbuf, "%.800s", err);
		    fclose(fp);
		}
	    }
	    break;
	}
	if (*errbuf) {
	    *error = dupstr(errbuf);
	    midend_free(fe->me);
	    sfree(fe);
#ifdef USE_PRINTING
            if (thegame.can_print) {
                drawing_free(print_fe->print_dr);
                sfree(print_fe);
            }
#endif
	    return NULL;
	}

    } else {
	midend_new_game(fe->me);
    }

    if (headless) {
        snaffle_colours(fe);
        get_size(fe, &fe->pw, &fe->ph);
        setup_backing_store(fe);
        return fe;
    }

#if !GTK_CHECK_VERSION(3,0,0)
    {
        /*
         * try_shrink_drawing_area() will do some fiddling with the
         * window size request (see comment in that function) after
         * all the bits and pieces such as the menu bar and status bar
         * have appeared in the puzzle window.
         *
         * However, on Unity systems, the menu bar _doesn't_ appear in
         * the puzzle window, because the Unity shell hijacks it into
         * the menu bar at the very top of the screen. We therefore
         * try to detect that situation here, so that we don't sit
         * here forever waiting for a menu bar.
         */
        const char prop[] = "gtk-shell-shows-menubar";
        GtkSettings *settings = gtk_settings_get_default();
        if (!g_object_class_find_property(G_OBJECT_GET_CLASS(settings),
                                          prop)) {
            fe->menubar_is_local = true;
        } else {
            int unity_mode;
            g_object_get(gtk_settings_get_default(),
                         prop, &unity_mode,
                         (const gchar *)NULL);
            fe->menubar_is_local = !unity_mode;
        }
    }
#endif

#if GTK_CHECK_VERSION(3,0,0)
    fe->awaiting_resize_ack = false;
#endif

    fe->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(fe->window), thegame.name);

    vbox = GTK_BOX(gtk_vbox_new(false, 0));
    gtk_container_add(GTK_CONTAINER(fe->window), GTK_WIDGET(vbox));
    gtk_widget_show(GTK_WIDGET(vbox));

    fe->dummy_accelgroup = gtk_accel_group_new();
    /*
     * Intentionally _not_ added to the window via
     * gtk_window_add_accel_group; see menu_key_event
     */

    hbox = GTK_BOX(gtk_hbox_new(false, 0));
    gtk_box_pack_start(vbox, GTK_WIDGET(hbox), false, false, 0);
    gtk_widget_show(GTK_WIDGET(hbox));

    fe->menubar = gtk_menu_bar_new();
    gtk_box_pack_start(hbox, fe->menubar, true, true, 0);
    gtk_widget_show(fe->menubar);

    menuitem = gtk_menu_item_new_with_mnemonic("_Game");
    gtk_container_add(GTK_CONTAINER(fe->menubar), menuitem);
    gtk_widget_show(menuitem);

    menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);

    add_menu_ui_item(fe, GTK_CONTAINER(menu), "New", UI_NEWGAME, 'n', 0);

    menuitem = gtk_menu_item_new_with_label("Restart");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(menu_restart_event), fe);
    gtk_widget_show(menuitem);

    menuitem = gtk_menu_item_new_with_label("Specific...");
    g_object_set_data(G_OBJECT(menuitem), "user-data",
                      GINT_TO_POINTER(CFG_DESC));
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(menu_config_event), fe);
    gtk_widget_show(menuitem);

    menuitem = gtk_menu_item_new_with_label("Random Seed...");
    g_object_set_data(G_OBJECT(menuitem), "user-data",
                      GINT_TO_POINTER(CFG_SEED));
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(menu_config_event), fe);
    gtk_widget_show(menuitem);

    fe->preset_radio = NULL;
    fe->preset_custom = NULL;
    fe->preset_threaded = false;

    preset_menu = midend_get_presets(fe->me, NULL);
    if (preset_menu->n_entries > 0 || thegame.can_configure) {
        GtkWidget *submenu;

        menuitem = gtk_menu_item_new_with_mnemonic("_Type");
        gtk_container_add(GTK_CONTAINER(fe->menubar), menuitem);
        gtk_widget_show(menuitem);

        submenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

        populate_gtk_preset_menu(fe, preset_menu, submenu);

	if (thegame.can_configure) {
	    menuitem = fe->preset_custom =
		gtk_radio_menu_item_new_with_label(fe->preset_radio,
						   "Custom...");
	    fe->preset_radio =
		gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));
            gtk_container_add(GTK_CONTAINER(submenu), menuitem);
            g_object_set_data(G_OBJECT(menuitem), "user-data",
                              GINT_TO_POINTER(CFG_SETTINGS));
            g_signal_connect(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(menu_config_event), fe);
            gtk_widget_show(menuitem);
	}

    }

    add_menu_separator(GTK_CONTAINER(menu));
    menuitem = gtk_menu_item_new_with_label("Load...");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(menu_load_event), fe);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label("Save...");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(menu_save_event), fe);
    gtk_widget_show(menuitem);
#ifdef USE_PRINTING
    if (thegame.can_print) {
        add_menu_separator(GTK_CONTAINER(menu));
        menuitem = gtk_menu_item_new_with_label("Print...");
        gtk_container_add(GTK_CONTAINER(menu), menuitem);
        g_signal_connect(G_OBJECT(menuitem), "activate",
                         G_CALLBACK(menu_print_event), print_fe);
        gtk_widget_show(menuitem);
    }
#endif
#ifndef STYLUS_BASED
    add_menu_separator(GTK_CONTAINER(menu));
    add_menu_ui_item(fe, GTK_CONTAINER(menu), "Undo", UI_UNDO, 'u', 0);
    add_menu_ui_item(fe, GTK_CONTAINER(menu), "Redo", UI_REDO, 'r', 0);
#endif
    if (thegame.can_format_as_text_ever) {
	add_menu_separator(GTK_CONTAINER(menu));
	menuitem = gtk_menu_item_new_with_label("Copy");
	gtk_container_add(GTK_CONTAINER(menu), menuitem);
	g_signal_connect(G_OBJECT(menuitem), "activate",
                         G_CALLBACK(menu_copy_event), fe);
	gtk_widget_show(menuitem);
	fe->copy_menu_item = menuitem;
    } else {
	fe->copy_menu_item = NULL;
    }
    if (thegame.can_solve) {
	add_menu_separator(GTK_CONTAINER(menu));
	menuitem = gtk_menu_item_new_with_label("Solve");
	gtk_container_add(GTK_CONTAINER(menu), menuitem);
	g_signal_connect(G_OBJECT(menuitem), "activate",
                         G_CALLBACK(menu_solve_event), fe);
	gtk_widget_show(menuitem);
    }
    add_menu_separator(GTK_CONTAINER(menu));
    add_menu_ui_item(fe, GTK_CONTAINER(menu), "Exit", UI_QUIT, 'q', 0);

    menuitem = gtk_menu_item_new_with_mnemonic("_Help");
    gtk_container_add(GTK_CONTAINER(fe->menubar), menuitem);
    gtk_widget_show(menuitem);

    menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);

    menuitem = gtk_menu_item_new_with_label("Contents");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    g_signal_connect(G_OBJECT(menuitem), "activate",
		     G_CALLBACK(menu_help_contents_event), fe);
    gtk_widget_show(menuitem);

    if (thegame.htmlhelp_topic) {
	char *item;
	assert(thegame.name);
	item = snewn(9 + strlen(thegame.name), char);
	sprintf(item, "Help on %s", thegame.name);
	menuitem = gtk_menu_item_new_with_label(item);
	sfree(item);
	gtk_container_add(GTK_CONTAINER(menu), menuitem);
	g_signal_connect(G_OBJECT(menuitem), "activate",
			 G_CALLBACK(menu_help_specific_event), fe);
	gtk_widget_show(menuitem);
    }

    menuitem = gtk_menu_item_new_with_label("About");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(menu_about_event), fe);
    gtk_widget_show(menuitem);

#ifdef STYLUS_BASED
    menuitem=gtk_button_new_with_mnemonic("_Redo");
    g_object_set_data(G_OBJECT(menuitem), "user-data",
                      GINT_TO_POINTER(UI_REDO));
    g_signal_connect(G_OBJECT(menuitem), "clicked",
                     G_CALLBACK(menu_key_event), fe);
    gtk_box_pack_end(hbox, menuitem, false, false, 0);
    gtk_widget_show(menuitem);

    menuitem=gtk_button_new_with_mnemonic("_Undo");
    g_object_set_data(G_OBJECT(menuitem), "user-data",
                      GINT_TO_POINTER(UI_UNDO));
    g_signal_connect(G_OBJECT(menuitem), "clicked",
                     G_CALLBACK(menu_key_event), fe);
    gtk_box_pack_end(hbox, menuitem, false, false, 0);
    gtk_widget_show(menuitem);

    if (thegame.flags & REQUIRE_NUMPAD) {
	hbox = GTK_BOX(gtk_hbox_new(false, 0));
	gtk_box_pack_start(vbox, GTK_WIDGET(hbox), false, false, 0);
	gtk_widget_show(GTK_WIDGET(hbox));

	*((int*)errbuf)=0;
	errbuf[1]='\0';
	for(errbuf[0]='0';errbuf[0]<='9';errbuf[0]++) {
	    menuitem=gtk_button_new_with_label(errbuf);
	    g_object_set_data(G_OBJECT(menuitem), "user-data",
                              GINT_TO_POINTER((int)(errbuf[0])));
	    g_signal_connect(G_OBJECT(menuitem), "clicked",
                             G_CALLBACK(menu_key_event), fe);
	    gtk_box_pack_start(hbox, menuitem, true, true, 0);
	    gtk_widget_show(menuitem);
	}
    }
#endif /* STYLUS_BASED */

    changed_preset(fe);

    snaffle_colours(fe);

    if (midend_wants_statusbar(fe->me)) {
	GtkWidget *viewport;
	GtkRequisition req;

	viewport = gtk_viewport_new(NULL, NULL);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
	fe->statusbar = gtk_statusbar_new();
	gtk_container_add(GTK_CONTAINER(viewport), fe->statusbar);
	gtk_widget_show(viewport);
	gtk_box_pack_end(vbox, viewport, false, false, 0);
	gtk_widget_show(fe->statusbar);
	fe->statusctx = gtk_statusbar_get_context_id
	    (GTK_STATUSBAR(fe->statusbar), "game");
	gtk_statusbar_push(GTK_STATUSBAR(fe->statusbar), fe->statusctx,
			   DEFAULT_STATUSBAR_TEXT);
#if GTK_CHECK_VERSION(3,0,0)
	gtk_widget_get_preferred_size(fe->statusbar, &req, NULL);
#else
	gtk_widget_size_request(fe->statusbar, &req);
#endif
	gtk_widget_set_size_request(viewport, -1, req.height);
    } else
	fe->statusbar = NULL;

    fe->area = gtk_drawing_area_new();
#if GTK_CHECK_VERSION(2,0,0) && !GTK_CHECK_VERSION(3,0,0)
    gtk_widget_set_double_buffered(fe->area, false);
#endif
    {
        GdkGeometry geom;
        geom.base_width = 0;
#if GTK_CHECK_VERSION(3,0,0)
        geom.base_height = window_extra_height(fe);
        gtk_window_set_geometry_hints(GTK_WINDOW(fe->window), NULL,
                                      &geom, GDK_HINT_BASE_SIZE);
#else
        geom.base_height = 0;
        gtk_window_set_geometry_hints(GTK_WINDOW(fe->window), fe->area,
                                      &geom, GDK_HINT_BASE_SIZE);
#endif
    }
    fe->w = -1;
    fe->h = -1;
    get_size(fe, &x, &y);
#if GTK_CHECK_VERSION(3,0,0)
    gtk_window_set_default_size(GTK_WINDOW(fe->window),
                                x, y + window_extra_height(fe));
#else
    fe->drawing_area_shrink_pending = false;
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
#endif

    gtk_box_pack_end(vbox, fe->area, true, true, 0);

    clear_backing_store(fe);
    fe->fonts = NULL;
    fe->nfonts = fe->fontsize = 0;

    fe->paste_data = NULL;
    fe->paste_data_len = 0;

    g_signal_connect(G_OBJECT(fe->window), "destroy",
                     G_CALLBACK(destroy), fe);
    g_signal_connect(G_OBJECT(fe->window), "key_press_event",
                     G_CALLBACK(key_event), fe);
    g_signal_connect(G_OBJECT(fe->area), "button_press_event",
                     G_CALLBACK(button_event), fe);
    g_signal_connect(G_OBJECT(fe->area), "button_release_event",
                     G_CALLBACK(button_event), fe);
    g_signal_connect(G_OBJECT(fe->area), "motion_notify_event",
                     G_CALLBACK(motion_event), fe);
    g_signal_connect(G_OBJECT(fe->window), "selection_get",
                     G_CALLBACK(selection_get), fe);
    g_signal_connect(G_OBJECT(fe->window), "selection_clear_event",
                     G_CALLBACK(selection_clear), fe);
#if GTK_CHECK_VERSION(3,0,0)
    g_signal_connect(G_OBJECT(fe->area), "draw",
                     G_CALLBACK(draw_area), fe);
#else
    g_signal_connect(G_OBJECT(fe->area), "expose_event",
                     G_CALLBACK(expose_area), fe);
#endif
    g_signal_connect(G_OBJECT(fe->window), "map_event",
                     G_CALLBACK(map_window), fe);
    g_signal_connect(G_OBJECT(fe->area), "configure_event",
                     G_CALLBACK(configure_area), fe);
    g_signal_connect(G_OBJECT(fe->window), "configure_event",
                     G_CALLBACK(configure_window), fe);
#if GTK_CHECK_VERSION(3,0,0)
    g_signal_connect(G_OBJECT(fe->window), "size_allocate",
                     G_CALLBACK(window_size_alloc), fe);
#endif

    gtk_widget_add_events(GTK_WIDGET(fe->area),
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
			  GDK_BUTTON_MOTION_MASK |
			  GDK_POINTER_MOTION_HINT_MASK);

    if (n_xpm_icons) {
        gtk_window_set_icon(GTK_WINDOW(fe->window),
                            gdk_pixbuf_new_from_xpm_data
                            ((const gchar **)xpm_icons[n_xpm_icons-1]));

	iconlist = NULL;
	for (n = 0; n < n_xpm_icons; n++) {
	    iconlist =
		g_list_append(iconlist,
			      gdk_pixbuf_new_from_xpm_data((const gchar **)
							   xpm_icons[n]));
	}
	gtk_window_set_icon_list(GTK_WINDOW(fe->window), iconlist);
    }

    gtk_widget_show(fe->area);
    gtk_widget_show(fe->window);

#if !GTK_CHECK_VERSION(3,0,0)
    fe->drawing_area_shrink_pending = true;
    try_shrink_drawing_area(fe);
#endif

    set_window_background(fe, 0);

    return fe;
}

static void list_presets_from_menu(struct preset_menu *menu)
{
    int i;

    for (i = 0; i < menu->n_entries; i++) {
        if (menu->entries[i].params) {
            char *paramstr = thegame.encode_params(
                menu->entries[i].params, true);
            printf("%s %s\n", paramstr, menu->entries[i].title);
            sfree(paramstr);
        } else {
            list_presets_from_menu(menu->entries[i].submenu);
        }
    }
}

int main(int argc, char **argv)
{
    char *pname = argv[0];
    int ngenerate = 0, px = 1, py = 1;
    bool print = false;
    bool time_generation = false, test_solve = false, list_presets = false;
    bool soln = false, colour = false;
    float scale = 1.0F;
    float redo_proportion = 0.0F;
    const char *savefile = NULL, *savesuffix = NULL;
    char *arg = NULL;
    int argtype = ARG_EITHER;
    char *screenshot_file = NULL;
    bool doing_opts = true;
    int ac = argc;
    char **av = argv;
    char errbuf[500];

    /*
     * Command line parsing in this function is rather fiddly,
     * because GTK wants to have a go at argc/argv _first_ - and
     * yet we can't let it, because gtk_init() will bomb out if it
     * can't open an X display, whereas in fact we want to permit
     * our --generate and --print modes to run without an X
     * display.
     * 
     * So what we do is:
     * 	- we parse the command line ourselves, without modifying
     * 	  argc/argv
     * 	- if we encounter an error which might plausibly be the
     * 	  result of a GTK command line (i.e. not detailed errors in
     * 	  particular options of ours) we store the error message
     * 	  and terminate parsing.
     * 	- if we got enough out of the command line to know it
     * 	  specifies a non-X mode of operation, we either display
     * 	  the stored error and return failure, or if there is no
     * 	  stored error we do the non-X operation and return
     * 	  success.
     *  - otherwise, we go straight to gtk_init().
     */

    errbuf[0] = '\0';
    while (--ac > 0) {
	char *p = *++av;
	if (doing_opts && !strcmp(p, "--version")) {
	    printf("%s, from Simon Tatham's Portable Puzzle Collection\n%s\n",
		   thegame.name, ver);
	    return 0;
	} else if (doing_opts && !strcmp(p, "--generate")) {
	    if (--ac > 0) {
		ngenerate = atoi(*++av);
		if (!ngenerate) {
		    fprintf(stderr, "%s: '--generate' expected a number\n",
			    pname);
		    return 1;
		}
	    } else
		ngenerate = 1;
	} else if (doing_opts && !strcmp(p, "--time-generation")) {
            time_generation = true;
	} else if (doing_opts && !strcmp(p, "--test-solve")) {
            test_solve = true;
	} else if (doing_opts && !strcmp(p, "--list-presets")) {
            list_presets = true;
	} else if (doing_opts && !strcmp(p, "--save")) {
	    if (--ac > 0) {
		savefile = *++av;
	    } else {
		fprintf(stderr, "%s: '--save' expected a filename\n",
			pname);
		return 1;
	    }
	} else if (doing_opts && (!strcmp(p, "--save-suffix") ||
				  !strcmp(p, "--savesuffix"))) {
	    if (--ac > 0) {
		savesuffix = *++av;
	    } else {
		fprintf(stderr, "%s: '--save-suffix' expected a filename\n",
			pname);
		return 1;
	    }
	} else if (doing_opts && !strcmp(p, "--print")) {
	    if (!thegame.can_print) {
		fprintf(stderr, "%s: this game does not support printing\n",
			pname);
		return 1;
	    }
	    print = true;
	    if (--ac > 0) {
		char *dim = *++av;
		if (sscanf(dim, "%dx%d", &px, &py) != 2) {
		    fprintf(stderr, "%s: unable to parse argument '%s' to "
			    "'--print'\n", pname, dim);
		    return 1;
		}
	    } else {
		px = py = 1;
	    }
	} else if (doing_opts && !strcmp(p, "--scale")) {
	    if (--ac > 0) {
		scale = atof(*++av);
	    } else {
		fprintf(stderr, "%s: no argument supplied to '--scale'\n",
			pname);
		return 1;
	    }
	} else if (doing_opts && !strcmp(p, "--redo")) {
	    /*
	     * This is an internal option which I don't expect
	     * users to have any particular use for. The effect of
	     * --redo is that once the game has been loaded and
	     * initialised, the next move in the redo chain is
	     * replayed, and the game screen is redrawn part way
	     * through the making of the move. This is only
	     * meaningful if there _is_ a next move in the redo
	     * chain, which means in turn that this option is only
	     * useful if you're also passing a save file on the
	     * command line.
	     *
	     * This option is used by the script which generates
	     * the puzzle icons and website screenshots, and I
	     * don't imagine it's useful for anything else.
	     * (Unless, I suppose, users don't like my screenshots
	     * and want to generate their own in the same way for
	     * some repackaged version of the puzzles.)
	     */
	    if (--ac > 0) {
		redo_proportion = atof(*++av);
	    } else {
		fprintf(stderr, "%s: no argument supplied to '--redo'\n",
			pname);
		return 1;
	    }
	} else if (doing_opts && !strcmp(p, "--screenshot")) {
	    /*
	     * Another internal option for the icon building
	     * script. This causes a screenshot of the central
	     * drawing area (i.e. not including the menu bar or
	     * status bar) to be saved to a PNG file once the
	     * window has been drawn, and then the application
	     * quits immediately.
	     */
	    if (--ac > 0) {
		screenshot_file = *++av;
	    } else {
		fprintf(stderr, "%s: no argument supplied to '--screenshot'\n",
			pname);
		return 1;
	    }
	} else if (doing_opts && (!strcmp(p, "--with-solutions") ||
				  !strcmp(p, "--with-solution") ||
				  !strcmp(p, "--with-solns") ||
				  !strcmp(p, "--with-soln") ||
				  !strcmp(p, "--solutions") ||
				  !strcmp(p, "--solution") ||
				  !strcmp(p, "--solns") ||
				  !strcmp(p, "--soln"))) {
	    soln = true;
	} else if (doing_opts && !strcmp(p, "--colour")) {
	    if (!thegame.can_print_in_colour) {
		fprintf(stderr, "%s: this game does not support colour"
			" printing\n", pname);
		return 1;
	    }
	    colour = true;
	} else if (doing_opts && !strcmp(p, "--load")) {
	    argtype = ARG_SAVE;
	} else if (doing_opts && !strcmp(p, "--game")) {
	    argtype = ARG_ID;
	} else if (doing_opts && !strcmp(p, "--")) {
	    doing_opts = false;
	} else if (!doing_opts || p[0] != '-') {
	    if (arg) {
		fprintf(stderr, "%s: more than one argument supplied\n",
			pname);
		return 1;
	    }
	    arg = p;
	} else {
	    sprintf(errbuf, "%.100s: unrecognised option '%.100s'\n",
		    pname, p);
	    break;
	}
    }

    /*
     * Special standalone mode for generating puzzle IDs on the
     * command line. Useful for generating puzzles to be printed
     * out and solved offline (for puzzles where that even makes
     * sense - Solo, for example, is a lot more pencil-and-paper
     * friendly than Twiddle!)
     * 
     * Usage:
     * 
     *   <puzzle-name> --generate [<n> [<params>]]
     * 
     * <n>, if present, is the number of puzzle IDs to generate.
     * <params>, if present, is the same type of parameter string
     * you would pass to the puzzle when running it in GUI mode,
     * including optional extras such as the expansion factor in
     * Rectangles and the difficulty level in Solo.
     * 
     * If you specify <params>, you must also specify <n> (although
     * you may specify it to be 1). Sorry; that was the
     * simplest-to-parse command-line syntax I came up with.
     */
    if (ngenerate > 0 || print || savefile || savesuffix) {
	int i, n = 1;
	midend *me;
	char *id;
	document *doc = NULL;

        /*
         * If we're in this branch, we should display any pending
         * error message from the command line, since GTK isn't going
         * to take another crack at making sense of it.
         */
        if (*errbuf) {
            fputs(errbuf, stderr);
            return 1;
        }

	n = ngenerate;

	me = midend_new(NULL, &thegame, NULL, NULL);
	i = 0;

	if (savefile && !savesuffix)
	    savesuffix = "";
	if (!savefile && savesuffix)
	    savefile = "";

	if (print)
	    doc = document_new(px, py, scale);

	/*
	 * In this loop, we either generate a game ID or read one
	 * from stdin depending on whether we're in generate mode;
	 * then we either write it to stdout or print it, depending
	 * on whether we're in print mode. Thus, this loop handles
	 * generate-to-stdout, print-from-stdin and generate-and-
	 * immediately-print modes.
	 * 
	 * (It could also handle a copy-stdin-to-stdout mode,
	 * although there's currently no combination of options
	 * which will cause this loop to be activated in that mode.
	 * It wouldn't be _entirely_ pointless, though, because
	 * stdin could contain bare params strings or random-seed
	 * IDs, and stdout would contain nothing but fully
	 * generated descriptive game IDs.)
	 */
	while (ngenerate == 0 || i < n) {
	    char *pstr, *seed;
            const char *err;
            struct rusage before, after;

	    if (ngenerate == 0) {
		pstr = fgetline(stdin);
		if (!pstr)
		    break;
		pstr[strcspn(pstr, "\r\n")] = '\0';
	    } else {
		if (arg) {
		    pstr = snewn(strlen(arg) + 40, char);

		    strcpy(pstr, arg);
		    if (i > 0 && strchr(arg, '#'))
			sprintf(pstr + strlen(pstr), "-%d", i);
		} else
		    pstr = NULL;
	    }

	    if (pstr) {
		err = midend_game_id(me, pstr);
		if (err) {
		    fprintf(stderr, "%s: error parsing '%s': %s\n",
			    pname, pstr, err);
		    return 1;
		}
	    }

            if (time_generation)
                getrusage(RUSAGE_SELF, &before);

            midend_new_game(me);

            seed = midend_get_random_seed(me);

            if (time_generation) {
                double elapsed;

                getrusage(RUSAGE_SELF, &after);

                elapsed = (after.ru_utime.tv_sec -
                           before.ru_utime.tv_sec);
                elapsed += (after.ru_utime.tv_usec -
                            before.ru_utime.tv_usec) / 1000000.0;

                printf("%s %s: %.6f\n", thegame.name, seed, elapsed);
            }

            if (test_solve && thegame.can_solve) {
                /*
                 * Now destroy the aux_info in the midend, by means of
                 * re-entering the same game id, and then try to solve
                 * it.
                 */
                char *game_id;

                game_id = midend_get_game_id(me);
                err = midend_game_id(me, game_id);
                if (err) {
                    fprintf(stderr, "%s %s: game id re-entry error: %s\n",
                            thegame.name, seed, err);
                    return 1;
                }
                midend_new_game(me);
                sfree(game_id);

                err = midend_solve(me);
                /*
                 * If the solve operation returned the error "Solution
                 * not known for this puzzle", that's OK, because that
                 * just means it's a puzzle for which we don't have an
                 * algorithmic solver and hence can't solve it without
                 * the aux_info, e.g. Netslide. Any other error is a
                 * problem, though.
                 */
                if (err && strcmp(err, "Solution not known for this puzzle")) {
                    fprintf(stderr, "%s %s: solve error: %s\n",
                            thegame.name, seed, err);
                    return 1;
                }
            }

	    sfree(pstr);
            sfree(seed);

	    if (doc) {
		err = midend_print_puzzle(me, doc, soln);
		if (err) {
		    fprintf(stderr, "%s: error in printing: %s\n", pname, err);
		    return 1;
		}
	    }
	    if (savefile) {
		struct savefile_write_ctx ctx;
		char *realname = snewn(40 + strlen(savefile) +
				       strlen(savesuffix), char);
		sprintf(realname, "%s%d%s", savefile, i, savesuffix);

                if (soln) {
                    const char *err = midend_solve(me);
                    if (err) {
                        fprintf(stderr, "%s: unable to show solution: %s\n",
                                realname, err);
                        return 1;
                    }
                }

		ctx.fp = fopen(realname, "w");
		if (!ctx.fp) {
		    fprintf(stderr, "%s: open: %s\n", realname,
			    strerror(errno));
		    return 1;
		}
                ctx.error = 0;
		midend_serialise(me, savefile_write, &ctx);
		if (ctx.error) {
		    fprintf(stderr, "%s: write: %s\n", realname,
			    strerror(ctx.error));
		    return 1;
		}
		if (fclose(ctx.fp)) {
		    fprintf(stderr, "%s: close: %s\n", realname,
			    strerror(errno));
		    return 1;
		}
		sfree(realname);
	    }
	    if (!doc && !savefile && !time_generation) {
		id = midend_get_game_id(me);
		puts(id);
		sfree(id);
	    }

	    i++;
	}

	if (doc) {
	    psdata *ps = ps_init(stdout, colour);
	    document_print(doc, ps_drawing_api(ps));
	    document_free(doc);
	    ps_free(ps);
	}

	midend_free(me);

	return 0;
    } else if (list_presets) {
        /*
         * Another specialist mode which causes the puzzle to list the
         * game_params strings for all its preset configurations.
         */
        midend *me;
        struct preset_menu *menu;

	me = midend_new(NULL, &thegame, NULL, NULL);
        menu = midend_get_presets(me, NULL);
        list_presets_from_menu(menu);
	midend_free(me);
        return 0;
    } else {
	frontend *fe;
        bool headless = screenshot_file != NULL;
        char *error = NULL;

        if (!headless)
            gtk_init(&argc, &argv);

	fe = new_window(arg, argtype, &error, headless);

	if (!fe) {
	    fprintf(stderr, "%s: %s\n", pname, error);
            sfree(error);
	    return 1;
	}

	if (screenshot_file) {
	    /*
	     * Some puzzles will not redraw their entire area if
	     * given a partially completed animation, which means
	     * we must redraw now and _then_ redraw again after
	     * freezing the move timer.
	     */
	    midend_force_redraw(fe->me);
	}

	if (redo_proportion) {
	    /* Start a redo. */
            midend_process_key(fe->me, 0, 0, 'r', NULL);
	    /* And freeze the timer at the specified position. */
	    midend_freeze_timer(fe->me, redo_proportion);
	}

	if (screenshot_file) {
	    save_screenshot_png(fe, screenshot_file);
	    exit(0);
	}

	gtk_main();
    }

    return 0;
}
