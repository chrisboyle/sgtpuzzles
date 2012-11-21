/*
 * gtk.c: GTK front end for my puzzle collection.
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <sys/time.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "puzzles.h"

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
#endif

/* #undef USE_CAIRO */
/* #define NO_THICK_LINE */
#ifdef DEBUGGING
static FILE *debug_fp = NULL;

void dputs(char *buf)
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

void debug_printf(char *fmt, ...)
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

void fatal(char *fmt, ...)
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
 * This structure holds all the data relevant to a single window.
 * In principle this would allow us to open multiple independent
 * puzzle windows, although I can't currently see any real point in
 * doing so. I'm just coding cleanly because there's no
 * particularly good reason not to.
 */
struct frontend {
    GtkWidget *window;
    GtkAccelGroup *accelgroup;
    GtkWidget *area;
    GtkWidget *statusbar;
    GtkWidget *menubar;
    guint statusctx;
    int w, h;
    midend *me;
#ifdef USE_CAIRO
    const float *colours;
    cairo_t *cr;
    cairo_surface_t *image;
    GdkPixmap *pixmap;
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
    int timer_active, timer_id;
    struct timeval last_time;
    struct font *fonts;
    int nfonts, fontsize;
    config_item *cfg;
    int cfg_which, cfgret;
    GtkWidget *cfgbox;
    void *paste_data;
    int paste_data_len;
    int pw, ph;                        /* pixmap size (w, h are area size) */
    int ox, oy;                        /* offset of pixmap in drawing area */
#ifdef OLD_FILESEL
    char *filesel_name;
#endif
    int drawing_area_shrink_pending;
    GSList *preset_radio;
    int n_preset_menu_items;
    int preset_threaded;
    GtkWidget *preset_custom;
    GtkWidget *copy_menu_item;
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
    GdkColor col = fe->window->style->bg[GTK_STATE_NORMAL];
    output[0] = col.red / 65535.0;
    output[1] = col.green / 65535.0;
    output[2] = col.blue / 65535.0;
}

void gtk_status_bar(void *handle, char *text)
{
    frontend *fe = (frontend *)handle;

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
    cairo_set_antialias(fe->cr, CAIRO_ANTIALIAS_GRAY);
    cairo_set_line_width(fe->cr, 1.0);
    cairo_set_line_cap(fe->cr, CAIRO_LINE_CAP_SQUARE);
    cairo_set_line_join(fe->cr, CAIRO_LINE_JOIN_ROUND);
}

static void teardown_drawing(frontend *fe)
{
    cairo_t *cr;

    cairo_destroy(fe->cr);
    fe->cr = NULL;

    cr = gdk_cairo_create(fe->pixmap);
    cairo_set_source_surface(cr, fe->image, 0, 0);
    cairo_rectangle(cr,
		    fe->bbox_l - 1,
		    fe->bbox_u - 1,
		    fe->bbox_r - fe->bbox_l + 2,
		    fe->bbox_d - fe->bbox_u + 2);
    cairo_fill(cr);
    cairo_destroy(cr);
}

static void snaffle_colours(frontend *fe)
{
    fe->colours = midend_colours(fe->me, &fe->ncolours);
}

static void set_colour(frontend *fe, int colour)
{
    cairo_set_source_rgb(fe->cr,
			 fe->colours[3*colour + 0],
			 fe->colours[3*colour + 1],
			 fe->colours[3*colour + 2]);
}

static void set_window_background(frontend *fe, int colour)
{
    GdkColormap *colmap;

    colmap = gdk_colormap_get_system();
    fe->background.red = fe->colours[3*colour + 0] * 65535;
    fe->background.green = fe->colours[3*colour + 1] * 65535;
    fe->background.blue = fe->colours[3*colour + 2] * 65535;
    if (!gdk_colormap_alloc_color(colmap, &fe->background, FALSE, FALSE)) {
	g_error("couldn't allocate background (#%02x%02x%02x)\n",
		fe->background.red >> 8, fe->background.green >> 8,
		fe->background.blue >> 8);
    }
    gdk_window_set_background(fe->area->window, &fe->background);
    gdk_window_set_background(fe->window->window, &fe->background);
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
    cairo_fill(fe->cr);
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

static void do_draw_poly(frontend *fe, int *coords, int npoints,
			 int fillcolour, int outlinecolour)
{
    int i;

    cairo_new_path(fe->cr);
    for (i = 0; i < npoints; i++)
	cairo_line_to(fe->cr, coords[i*2] + 0.5, coords[i*2 + 1] + 0.5);
    cairo_close_path(fe->cr);
    if (fillcolour >= 0) {
        set_colour(fe, fillcolour);
	cairo_fill_preserve(fe->cr);
    }
    assert(outlinecolour >= 0);
    set_colour(fe, outlinecolour);
    cairo_stroke(fe->cr);
}

static void do_draw_circle(frontend *fe, int cx, int cy, int radius,
			   int fillcolour, int outlinecolour)
{
    cairo_new_path(fe->cr);
    cairo_arc(fe->cr, cx + 0.5, cy + 0.5, radius, 0, 2*PI);
    cairo_close_path(fe->cr);		/* Just in case... */
    if (fillcolour >= 0) {
	set_colour(fe, fillcolour);
	cairo_fill_preserve(fe->cr);
    }
    assert(outlinecolour >= 0);
    set_colour(fe, outlinecolour);
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

static void setup_backing_store(frontend *fe)
{
    cairo_t *cr;
    int i;

    fe->pixmap = gdk_pixmap_new(fe->area->window, fe->pw, fe->ph, -1);
    fe->image = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
					   fe->pw, fe->ph);

    for (i = 0; i < 3; i++) {
	switch (i) {
	    case 0: cr = cairo_create(fe->image); break;
	    case 1: cr = gdk_cairo_create(fe->pixmap); break;
	    case 2: cr = gdk_cairo_create(fe->area->window); break;
	}
	cairo_set_source_rgb(cr,
			     fe->colours[0], fe->colours[1], fe->colours[2]);
	cairo_paint(cr);
	cairo_destroy(cr);
    }
}

static int backing_store_ok(frontend *fe)
{
    return (!!fe->image);
}

static void teardown_backing_store(frontend *fe)
{
    cairo_surface_destroy(fe->image);
    gdk_pixmap_unref(fe->pixmap);
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
			      FALSE, FALSE, success);
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

static void set_colour(frontend *fe, int colour)
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

static void do_draw_poly(frontend *fe, int *coords, int npoints,
			 int fillcolour, int outlinecolour)
{
    GdkPoint *points = snewn(npoints, GdkPoint);
    int i;

    for (i = 0; i < npoints; i++) {
        points[i].x = coords[i*2];
        points[i].y = coords[i*2+1];
    }

    if (fillcolour >= 0) {
	set_colour(fe, fillcolour);
	gdk_draw_polygon(fe->pixmap, fe->gc, TRUE, points, npoints);
    }
    assert(outlinecolour >= 0);
    set_colour(fe, outlinecolour);

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
	set_colour(fe, fillcolour);
	gdk_draw_arc(fe->pixmap, fe->gc, TRUE,
		     cx - radius, cy - radius,
		     2 * radius, 2 * radius, 0, 360 * 64);
    }

    assert(outlinecolour >= 0);
    set_colour(fe, outlinecolour);
    gdk_draw_arc(fe->pixmap, fe->gc, FALSE,
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

static void repaint_rectangle(frontend *fe, GtkWidget *widget,
			      int x, int y, int w, int h)
{
    GdkGC *gc = gdk_gc_new(widget->window);
#ifdef USE_CAIRO
    gdk_gc_set_foreground(gc, &fe->background);
#else
    gdk_gc_set_foreground(gc, &fe->colours[fe->backgroundindex]);
#endif
    if (x < fe->ox) {
	gdk_draw_rectangle(widget->window, gc,
			   TRUE, x, y, fe->ox - x, h);
	w -= (fe->ox - x);
	x = fe->ox;
    }
    if (y < fe->oy) {
	gdk_draw_rectangle(widget->window, gc,
			   TRUE, x, y, w, fe->oy - y);
	h -= (fe->oy - y);
	y = fe->oy;
    }
    if (w > fe->pw) {
	gdk_draw_rectangle(widget->window, gc,
			   TRUE, x + fe->pw, y, w - fe->pw, h);
	w = fe->pw;
    }
    if (h > fe->ph) {
	gdk_draw_rectangle(widget->window, gc,
			   TRUE, x, y + fe->ph, w, h - fe->ph);
	h = fe->ph;
    }
    gdk_draw_pixmap(widget->window, gc, fe->pixmap,
		    x - fe->ox, y - fe->oy, x, y, w, h);
    gdk_gc_unref(gc);
}

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

void gtk_start_draw(void *handle)
{
    frontend *fe = (frontend *)handle;
    fe->bbox_l = fe->w;
    fe->bbox_r = 0;
    fe->bbox_u = fe->h;
    fe->bbox_d = 0;
    setup_drawing(fe);
}

void gtk_clip(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    do_clip(fe, x, y, w, h);
}

void gtk_unclip(void *handle)
{
    frontend *fe = (frontend *)handle;
    do_unclip(fe);
}

void gtk_draw_text(void *handle, int x, int y, int fonttype, int fontsize,
		   int align, int colour, char *text)
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
    set_colour(fe, colour);
    align_and_draw_text(fe, i, align, x, y, text);
}

void gtk_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
    frontend *fe = (frontend *)handle;
    set_colour(fe, colour);
    do_draw_rect(fe, x, y, w, h);
}

void gtk_draw_line(void *handle, int x1, int y1, int x2, int y2, int colour)
{
    frontend *fe = (frontend *)handle;
    set_colour(fe, colour);
    do_draw_line(fe, x1, y1, x2, y2);
}

void gtk_draw_thick_line(void *handle, float thickness,
			 float x1, float y1, float x2, float y2, int colour)
{
    frontend *fe = (frontend *)handle;
    set_colour(fe, colour);
    do_draw_thick_line(fe, thickness, x1, y1, x2, y2);
}

void gtk_draw_poly(void *handle, int *coords, int npoints,
		   int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    do_draw_poly(fe, coords, npoints, fillcolour, outlinecolour);
}

void gtk_draw_circle(void *handle, int cx, int cy, int radius,
		     int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    do_draw_circle(fe, cx, cy, radius, fillcolour, outlinecolour);
}

blitter *gtk_blitter_new(void *handle, int w, int h)
{
    blitter *bl = snew(blitter);
    setup_blitter(bl, w, h);
    bl->w = w;
    bl->h = h;
    return bl;
}

void gtk_blitter_free(void *handle, blitter *bl)
{
    teardown_blitter(bl);
    sfree(bl);
}

void gtk_blitter_save(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    do_blitter_save(fe, bl, x, y);
    bl->x = x;
    bl->y = y;
}

void gtk_blitter_load(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    if (x == BLITTER_FROMSAVED && y == BLITTER_FROMSAVED) {
        x = bl->x;
        y = bl->y;
    }
    do_blitter_load(fe, bl, x, y);
}

void gtk_draw_update(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    if (fe->bbox_l > x  ) fe->bbox_l = x  ;
    if (fe->bbox_r < x+w) fe->bbox_r = x+w;
    if (fe->bbox_u > y  ) fe->bbox_u = y  ;
    if (fe->bbox_d < y+h) fe->bbox_d = y+h;
}

void gtk_end_draw(void *handle)
{
    frontend *fe = (frontend *)handle;

    teardown_drawing(fe);

    if (fe->bbox_l < fe->bbox_r && fe->bbox_u < fe->bbox_d) {
	repaint_rectangle(fe, fe->area,
			  fe->bbox_l - 1 + fe->ox,
			  fe->bbox_u - 1 + fe->oy,
			  fe->bbox_r - fe->bbox_l + 2,
			  fe->bbox_d - fe->bbox_u + 2);
    }
}

#ifdef USE_PANGO
char *gtk_text_fallback(void *handle, const char *const *strings, int nstrings)
{
    /*
     * We assume Pango can cope with any UTF-8 likely to be emitted
     * by a puzzle.
     */
    return dupstr(strings[0]);
}
#endif

const struct drawing_api gtk_drawing = {
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
    NULL, NULL, NULL, NULL, NULL, NULL, /* {begin,end}_{doc,page,puzzle} */
    NULL, NULL,			       /* line_width, line_dotted */
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
        return TRUE;

#if !GTK_CHECK_VERSION(2,0,0)
    /* Gtk 1.2 passes a key event to this function even if it's also
     * defined as an accelerator.
     * Gtk 2 doesn't do this, and this function appears not to exist there. */
    if (fe->accelgroup &&
        gtk_accel_group_get_entry(fe->accelgroup,
        event->keyval, event->state))
        return TRUE;
#endif

    /* Handle mnemonics. */
    if (gtk_window_activate_key(GTK_WINDOW(fe->window), event))
        return TRUE;

    if (event->keyval == GDK_Up)
        keyval = shift | ctrl | CURSOR_UP;
    else if (event->keyval == GDK_KP_Up || event->keyval == GDK_KP_8)
	keyval = MOD_NUM_KEYPAD | '8';
    else if (event->keyval == GDK_Down)
        keyval = shift | ctrl | CURSOR_DOWN;
    else if (event->keyval == GDK_KP_Down || event->keyval == GDK_KP_2)
	keyval = MOD_NUM_KEYPAD | '2';
    else if (event->keyval == GDK_Left)
        keyval = shift | ctrl | CURSOR_LEFT;
    else if (event->keyval == GDK_KP_Left || event->keyval == GDK_KP_4)
	keyval = MOD_NUM_KEYPAD | '4';
    else if (event->keyval == GDK_Right)
        keyval = shift | ctrl | CURSOR_RIGHT;
    else if (event->keyval == GDK_KP_Right || event->keyval == GDK_KP_6)
	keyval = MOD_NUM_KEYPAD | '6';
    else if (event->keyval == GDK_KP_Home || event->keyval == GDK_KP_7)
        keyval = MOD_NUM_KEYPAD | '7';
    else if (event->keyval == GDK_KP_End || event->keyval == GDK_KP_1)
        keyval = MOD_NUM_KEYPAD | '1';
    else if (event->keyval == GDK_KP_Page_Up || event->keyval == GDK_KP_9)
        keyval = MOD_NUM_KEYPAD | '9';
    else if (event->keyval == GDK_KP_Page_Down || event->keyval == GDK_KP_3)
        keyval = MOD_NUM_KEYPAD | '3';
    else if (event->keyval == GDK_KP_Insert || event->keyval == GDK_KP_0)
        keyval = MOD_NUM_KEYPAD | '0';
    else if (event->keyval == GDK_KP_Begin || event->keyval == GDK_KP_5)
        keyval = MOD_NUM_KEYPAD | '5';
    else if (event->keyval == GDK_BackSpace ||
	     event->keyval == GDK_Delete ||
	     event->keyval == GDK_KP_Delete)
        keyval = '\177';
    else if (event->string[0] && !event->string[1])
        keyval = (unsigned char)event->string[0];
    else
        keyval = -1;

    if (keyval >= 0 &&
        !midend_process_key(fe->me, 0, 0, keyval))
	gtk_widget_destroy(fe->window);

    return TRUE;
}

static gint button_event(GtkWidget *widget, GdkEventButton *event,
                         gpointer data)
{
    frontend *fe = (frontend *)data;
    int button;

    if (!backing_store_ok(fe))
        return TRUE;

    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE)
        return TRUE;

    if (event->button == 2 || (event->state & GDK_SHIFT_MASK))
	button = MIDDLE_BUTTON;
    else if (event->button == 3 || (event->state & GDK_MOD1_MASK))
	button = RIGHT_BUTTON;
    else if (event->button == 1)
	button = LEFT_BUTTON;
    else
	return FALSE;		       /* don't even know what button! */

    if (event->type == GDK_BUTTON_RELEASE)
        button += LEFT_RELEASE - LEFT_BUTTON;

    if (!midend_process_key(fe->me, event->x - fe->ox,
                            event->y - fe->oy, button))
	gtk_widget_destroy(fe->window);

    return TRUE;
}

static gint motion_event(GtkWidget *widget, GdkEventMotion *event,
                         gpointer data)
{
    frontend *fe = (frontend *)data;
    int button;

    if (!backing_store_ok(fe))
        return TRUE;

    if (event->state & (GDK_BUTTON2_MASK | GDK_SHIFT_MASK))
	button = MIDDLE_DRAG;
    else if (event->state & GDK_BUTTON1_MASK)
	button = LEFT_DRAG;
    else if (event->state & GDK_BUTTON3_MASK)
	button = RIGHT_DRAG;
    else
	return FALSE;		       /* don't even know what button! */

    if (!midend_process_key(fe->me, event->x - fe->ox,
                            event->y - fe->oy, button))
	gtk_widget_destroy(fe->window);
#if GTK_CHECK_VERSION(2,12,0)
    gdk_event_request_motions(event);
#else
    gdk_window_get_pointer(widget->window, NULL, NULL, NULL);
#endif

    return TRUE;
}

static gint expose_area(GtkWidget *widget, GdkEventExpose *event,
                        gpointer data)
{
    frontend *fe = (frontend *)data;

    if (backing_store_ok(fe)) {
	repaint_rectangle(fe, widget,
			  event->area.x, event->area.y,
			  event->area.width, event->area.height);
    }
    return TRUE;
}

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

    return TRUE;
}

static gint configure_area(GtkWidget *widget,
                           GdkEventConfigure *event, gpointer data)
{
    frontend *fe = (frontend *)data;
    int x, y;

    if (backing_store_ok(fe))
	teardown_backing_store(fe);

    x = fe->w = event->width;
    y = fe->h = event->height;
    midend_size(fe->me, &x, &y, TRUE);
    fe->pw = x;
    fe->ph = y;
    fe->ox = (fe->w - fe->pw) / 2;
    fe->oy = (fe->h - fe->ph) / 2;

    setup_backing_store(fe);
    midend_force_redraw(fe->me);

    return TRUE;
}

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
        gtk_timeout_remove(fe->timer_id);
    fe->timer_active = FALSE;
}

void activate_timer(frontend *fe)
{
    if (!fe)
	return;			       /* can happen due to --generate */
    if (!fe->timer_active) {
        fe->timer_id = gtk_timeout_add(20, timer_func, fe);
	gettimeofday(&fe->last_time, NULL);
    }
    fe->timer_active = TRUE;
}

static void window_destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

static void msgbox_button_clicked(GtkButton *button, gpointer data)
{
    GtkWidget *window = GTK_WIDGET(data);
    int v, *ip;

    ip = (int *)gtk_object_get_data(GTK_OBJECT(window), "user-data");
    v = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(button), "user-data"));
    *ip = v;

    gtk_widget_destroy(GTK_WIDGET(data));
}

static int win_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GtkObject *cancelbutton = GTK_OBJECT(data);

    /*
     * `Escape' effectively clicks the cancel button
     */
    if (event->keyval == GDK_Escape) {
	gtk_signal_emit_by_name(GTK_OBJECT(cancelbutton), "clicked");
	return TRUE;
    }

    return FALSE;
}

enum { MB_OK, MB_YESNO };

int message_box(GtkWidget *parent, char *title, char *msg, int centre,
		int type)
{
    GtkWidget *window, *hbox, *text, *button;
    char *titles;
    int i, def, cancel;

    window = gtk_dialog_new();
    text = gtk_label_new(msg);
    gtk_misc_set_alignment(GTK_MISC(text), 0.0, 0.0);
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), text, FALSE, FALSE, 20);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
                       hbox, FALSE, FALSE, 20);
    gtk_widget_show(text);
    gtk_widget_show(hbox);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);

    if (type == MB_OK) {
	titles = GTK_STOCK_OK "\0";
	def = cancel = 0;
    } else {
	assert(type == MB_YESNO);
	titles = GTK_STOCK_NO "\0" GTK_STOCK_YES "\0";
	def = 1;
	cancel = 0;
    }
    i = 0;
    
    while (*titles) {
	button = gtk_button_new_from_stock(titles);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(window)->action_area),
			 button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	if (i == def) {
	    GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	    gtk_window_set_default(GTK_WINDOW(window), button);
	}
	if (i == cancel) {
	    gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
			       GTK_SIGNAL_FUNC(win_key_press), button);
	}
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   GTK_SIGNAL_FUNC(msgbox_button_clicked), window);
	gtk_object_set_data(GTK_OBJECT(button), "user-data",
			    GINT_TO_POINTER(i));
	titles += strlen(titles)+1;
	i++;
    }
    gtk_object_set_data(GTK_OBJECT(window), "user-data",
			GINT_TO_POINTER(&i));
    gtk_signal_connect(GTK_OBJECT(window), "destroy",
                       GTK_SIGNAL_FUNC(window_destroy), NULL);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent));
    /* set_transient_window_pos(parent, window); */
    gtk_widget_show(window);
    i = -1;
    gtk_main();
    return (type == MB_YESNO ? i == 1 : TRUE);
}

void error_box(GtkWidget *parent, char *msg)
{
    message_box(parent, "Error", msg, FALSE, MB_OK);
}

static void config_ok_button_clicked(GtkButton *button, gpointer data)
{
    frontend *fe = (frontend *)data;
    char *err;

    err = midend_set_config(fe->me, fe->cfg_which, fe->cfg);

    if (err)
	error_box(fe->cfgbox, err);
    else {
	fe->cfgret = TRUE;
	gtk_widget_destroy(fe->cfgbox);
	changed_preset(fe);
    }
}

static void config_cancel_button_clicked(GtkButton *button, gpointer data)
{
    frontend *fe = (frontend *)data;

    gtk_widget_destroy(fe->cfgbox);
}

static int editbox_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
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
    if (event->keyval == GDK_Return && widget->parent != NULL) {
	gint return_val;
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
	gtk_signal_emit_by_name(GTK_OBJECT(widget->parent), "key_press_event",
				event, &return_val);
	return return_val;
    }
    return FALSE;
}

static void editbox_changed(GtkEditable *ed, gpointer data)
{
    config_item *i = (config_item *)data;

    sfree(i->sval);
    i->sval = dupstr(gtk_entry_get_text(GTK_ENTRY(ed)));
}

static void button_toggled(GtkToggleButton *tb, gpointer data)
{
    config_item *i = (config_item *)data;

    i->ival = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tb));
}

static void droplist_sel(GtkMenuItem *item, gpointer data)
{
    config_item *i = (config_item *)data;

    i->ival = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(item),
						  "user-data"));
}

static int get_config(frontend *fe, int which)
{
    GtkWidget *w, *table, *cancel;
    char *title;
    config_item *i;
    int y;

    fe->cfg = midend_get_config(fe->me, which, &title);
    fe->cfg_which = which;
    fe->cfgret = FALSE;

    fe->cfgbox = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(fe->cfgbox), title);
    sfree(title);

    w = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(fe->cfgbox)->action_area),
                     w, FALSE, FALSE, 0);
    gtk_widget_show(w);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
                       GTK_SIGNAL_FUNC(config_cancel_button_clicked), fe);
    cancel = w;

    w = gtk_button_new_from_stock(GTK_STOCK_OK);
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(fe->cfgbox)->action_area),
                     w, FALSE, FALSE, 0);
    gtk_widget_show(w);
    GTK_WIDGET_SET_FLAGS(w, GTK_CAN_DEFAULT);
    gtk_window_set_default(GTK_WINDOW(fe->cfgbox), w);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
                       GTK_SIGNAL_FUNC(config_ok_button_clicked), fe);

    table = gtk_table_new(1, 2, FALSE);
    y = 0;
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(fe->cfgbox)->vbox),
                     table, FALSE, FALSE, 0);
    gtk_widget_show(table);

    for (i = fe->cfg; i->type != C_END; i++) {
	gtk_table_resize(GTK_TABLE(table), y+1, 2);

	switch (i->type) {
	  case C_STRING:
	    /*
	     * Edit box with a label beside it.
	     */

	    w = gtk_label_new(i->name);
	    gtk_misc_set_alignment(GTK_MISC(w), 0.0, 0.5);
	    gtk_table_attach(GTK_TABLE(table), w, 0, 1, y, y+1,
			     GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     3, 3);
	    gtk_widget_show(w);

	    w = gtk_entry_new();
	    gtk_table_attach(GTK_TABLE(table), w, 1, 2, y, y+1,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     3, 3);
	    gtk_entry_set_text(GTK_ENTRY(w), i->sval);
	    gtk_signal_connect(GTK_OBJECT(w), "changed",
			       GTK_SIGNAL_FUNC(editbox_changed), i);
	    gtk_signal_connect(GTK_OBJECT(w), "key_press_event",
			       GTK_SIGNAL_FUNC(editbox_key), NULL);
	    gtk_widget_show(w);

	    break;

	  case C_BOOLEAN:
	    /*
	     * Simple checkbox.
	     */
            w = gtk_check_button_new_with_label(i->name);
	    gtk_signal_connect(GTK_OBJECT(w), "toggled",
			       GTK_SIGNAL_FUNC(button_toggled), i);
	    gtk_table_attach(GTK_TABLE(table), w, 0, 2, y, y+1,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     3, 3);
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), i->ival);
	    gtk_widget_show(w);
	    break;

	  case C_CHOICES:
	    /*
	     * Drop-down list (GtkOptionMenu).
	     */

	    w = gtk_label_new(i->name);
	    gtk_misc_set_alignment(GTK_MISC(w), 0.0, 0.5);
	    gtk_table_attach(GTK_TABLE(table), w, 0, 1, y, y+1,
			     GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL ,
			     3, 3);
	    gtk_widget_show(w);

	    w = gtk_option_menu_new();
	    gtk_table_attach(GTK_TABLE(table), w, 1, 2, y, y+1,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     3, 3);
	    gtk_widget_show(w);

	    {
		int c, val;
		char *p, *q, *name;
		GtkWidget *menuitem;
		GtkWidget *menu = gtk_menu_new();

		gtk_option_menu_set_menu(GTK_OPTION_MENU(w), menu);

		c = *i->sval;
		p = i->sval+1;
		val = 0;

		while (*p) {
		    q = p;
		    while (*q && *q != c)
			q++;

		    name = snewn(q-p+1, char);
		    strncpy(name, p, q-p);
		    name[q-p] = '\0';

		    if (*q) q++;       /* eat delimiter */

		    menuitem = gtk_menu_item_new_with_label(name);
		    gtk_container_add(GTK_CONTAINER(menu), menuitem);
		    gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
					GINT_TO_POINTER(val));
		    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
				       GTK_SIGNAL_FUNC(droplist_sel), i);
		    gtk_widget_show(menuitem);

		    val++;

		    p = q;
		}

		gtk_option_menu_set_history(GTK_OPTION_MENU(w), i->ival);
	    }

	    break;
	}

	y++;
    }

    gtk_signal_connect(GTK_OBJECT(fe->cfgbox), "destroy",
                       GTK_SIGNAL_FUNC(window_destroy), NULL);
    gtk_signal_connect(GTK_OBJECT(fe->cfgbox), "key_press_event",
		       GTK_SIGNAL_FUNC(win_key_press), cancel);
    gtk_window_set_modal(GTK_WINDOW(fe->cfgbox), TRUE);
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
    int key = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(menuitem),
                                                  "user-data"));
    if (!midend_process_key(fe->me, 0, 0, key))
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
    midend_size(fe->me, &x, &y, FALSE);
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

    fe->preset_threaded = TRUE;
    if (n < 0 && fe->preset_custom) {
	gtk_check_menu_item_set_active(
	    GTK_CHECK_MENU_ITEM(fe->preset_custom),
	    TRUE);
    } else {
	GSList *gs = fe->preset_radio;
	int i = fe->n_preset_menu_items - 1 - n;
	if (fe->preset_custom)
	    gs = gs->next;
	while (i && gs) {
	    i--;
	    gs = gs->next;
	}
	if (gs) {
	    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gs->data),
					   TRUE);
	} else for (gs = fe->preset_radio; gs; gs = gs->next) {
	    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gs->data),
					   FALSE);
	}
    }
    fe->preset_threaded = FALSE;

    /*
     * Update the greying on the Copy menu option.
     */
    if (fe->copy_menu_item) {
	int enabled = midend_can_format_as_text_now(fe->me);
	gtk_widget_set_sensitive(fe->copy_menu_item, enabled);
    }
}

static gboolean not_size_allocated_yet(GtkWidget *w)
{
    /*
     * This function tests whether a widget has not yet taken up space
     * on the screen which it will occupy in future. (Therefore, it
     * returns true only if the widget does exist but does not have a
     * size allocation. A null widget is already taking up all the
     * space it ever will.)
     */
    if (!w)
        return FALSE;        /* nonexistent widgets aren't a problem */

#if GTK_CHECK_VERSION(2,18,0)  /* skip if no gtk_widget_get_allocation */
    {
        GtkAllocation a;
        gtk_widget_get_allocation(w, &a);
        if (a.height == 0 || a.width == 0)
            return TRUE;       /* widget exists but has no size yet */
    }
#endif

    return FALSE;
}

static void try_shrink_drawing_area(frontend *fe)
{
    if (fe->drawing_area_shrink_pending &&
        !not_size_allocated_yet(fe->menubar) &&
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
        fe->drawing_area_shrink_pending = FALSE;
    }
}

static gint configure_window(GtkWidget *widget,
                             GdkEventConfigure *event, gpointer data)
{
    frontend *fe = (frontend *)data;
    /*
     * When the main puzzle window changes size, it might be because
     * the menu bar or status bar has turned up after starting off
     * absent, in which case we should have another go at enacting a
     * pending shrink of the drawing area.
     */
    try_shrink_drawing_area(fe);
    return FALSE;
}

static void resize_fe(frontend *fe)
{
    int x, y;

    get_size(fe, &x, &y);
    fe->w = x;
    fe->h = y;
    fe->drawing_area_shrink_pending = FALSE;
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    {
        GtkRequisition req;
        gtk_widget_size_request(GTK_WIDGET(fe->window), &req);
        gtk_window_resize(GTK_WINDOW(fe->window), req.width, req.height);
    }
    fe->drawing_area_shrink_pending = TRUE;
    try_shrink_drawing_area(fe);
}

static void menu_preset_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    game_params *params =
        (game_params *)gtk_object_get_data(GTK_OBJECT(menuitem), "user-data");

    if (fe->preset_threaded ||
	(GTK_IS_CHECK_MENU_ITEM(menuitem) &&
	 !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))))
	return;
    midend_set_params(fe->me, params);
    midend_new_game(fe->me);
    changed_preset(fe);
    resize_fe(fe);
}

GdkAtom compound_text_atom, utf8_string_atom;
int paste_initialised = FALSE;

static void set_selection(frontend *fe, GdkAtom selection)
{
    if (!paste_initialised) {
	compound_text_atom = gdk_atom_intern("COMPOUND_TEXT", FALSE);
	utf8_string_atom = gdk_atom_intern("UTF8_STRING", FALSE);
	paste_initialised = TRUE;
    }

    /*
     * For this simple application we can safely assume that the
     * data passed to this function is pure ASCII, which means we
     * can return precisely the same stuff for types STRING,
     * COMPOUND_TEXT or UTF8_STRING.
     */

    if (gtk_selection_owner_set(fe->area, selection, CurrentTime)) {
	gtk_selection_clear_targets(fe->area, selection);
	gtk_selection_add_target(fe->area, selection,
				 GDK_SELECTION_TYPE_STRING, 1);
	gtk_selection_add_target(fe->area, selection, compound_text_atom, 1);
	gtk_selection_add_target(fe->area, selection, utf8_string_atom, 1);
    }
}

void write_clip(frontend *fe, char *data)
{
    if (fe->paste_data)
	sfree(fe->paste_data);

    fe->paste_data = data;
    fe->paste_data_len = strlen(data);

    set_selection(fe, GDK_SELECTION_PRIMARY);
    set_selection(fe, GDK_SELECTION_CLIPBOARD);
}

void selection_get(GtkWidget *widget, GtkSelectionData *seldata,
		   guint info, guint time_stamp, gpointer data)
{
    frontend *fe = (frontend *)data;
    gtk_selection_data_set(seldata, seldata->target, 8,
			   fe->paste_data, fe->paste_data_len);
}

gint selection_clear(GtkWidget *widget, GdkEventSelection *seldata,
		     gpointer data)
{
    frontend *fe = (frontend *)data;

    if (fe->paste_data)
	sfree(fe->paste_data);
    fe->paste_data = NULL;
    fe->paste_data_len = 0;
    return TRUE;
}

static void menu_copy_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    char *text;

    text = midend_text_format(fe->me);

    if (text) {
	write_clip(fe, text);
    } else {
	gdk_beep();
    }
}

#ifdef OLD_FILESEL

static void filesel_ok(GtkButton *button, gpointer data)
{
    frontend *fe = (frontend *)data;

    gpointer filesel = gtk_object_get_data(GTK_OBJECT(button), "user-data");

    const char *name =
        gtk_file_selection_get_filename(GTK_FILE_SELECTION(filesel));

    fe->filesel_name = dupstr(name);
}

static char *file_selector(frontend *fe, char *title, int save)
{
    GtkWidget *filesel =
        gtk_file_selection_new(title);

    fe->filesel_name = NULL;

    gtk_window_set_modal(GTK_WINDOW(filesel), TRUE);
    gtk_object_set_data
        (GTK_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "user-data",
         (gpointer)filesel);
    gtk_signal_connect
        (GTK_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "clicked",
         GTK_SIGNAL_FUNC(filesel_ok), fe);
    gtk_signal_connect_object
        (GTK_OBJECT(GTK_FILE_SELECTION(filesel)->ok_button), "clicked",
         GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer)filesel);
    gtk_signal_connect_object
        (GTK_OBJECT(GTK_FILE_SELECTION(filesel)->cancel_button), "clicked",
         GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer)filesel);
    gtk_signal_connect(GTK_OBJECT(filesel), "destroy",
                       GTK_SIGNAL_FUNC(window_destroy), NULL);
    gtk_widget_show(filesel);
    gtk_window_set_transient_for(GTK_WINDOW(filesel), GTK_WINDOW(fe->window));
    gtk_main();

    return fe->filesel_name;
}

#else

static char *file_selector(frontend *fe, char *title, int save)
{
    char *filesel_name = NULL;

    GtkWidget *filesel =
        gtk_file_chooser_dialog_new(title,
				    GTK_WINDOW(fe->window),
				    save ? GTK_FILE_CHOOSER_ACTION_SAVE :
				    GTK_FILE_CHOOSER_ACTION_OPEN,
				    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				    save ? GTK_STOCK_SAVE : GTK_STOCK_OPEN,
				    GTK_RESPONSE_ACCEPT,
				    NULL);

    if (gtk_dialog_run(GTK_DIALOG(filesel)) == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(filesel));
        filesel_name = dupstr(name);
    }

    gtk_widget_destroy(filesel);

    return filesel_name;
}

#endif

struct savefile_write_ctx {
    FILE *fp;
    int error;
};

static void savefile_write(void *wctx, void *buf, int len)
{
    struct savefile_write_ctx *ctx = (struct savefile_write_ctx *)wctx;
    if (fwrite(buf, 1, len, ctx->fp) < len)
	ctx->error = errno;
}

static int savefile_read(void *wctx, void *buf, int len)
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

    name = file_selector(fe, "Enter name of game file to save", TRUE);

    if (name) {
        FILE *fp;

	if ((fp = fopen(name, "r")) != NULL) {
	    char buf[256 + FILENAME_MAX];
	    fclose(fp);
	    /* file exists */

	    sprintf(buf, "Are you sure you want to overwrite the"
		    " file \"%.*s\"?",
		    FILENAME_MAX, name);
	    if (!message_box(fe->window, "Question", buf, TRUE, MB_YESNO))
		return;
	}

	fp = fopen(name, "w");
        sfree(name);

        if (!fp) {
            error_box(fe->window, "Unable to open save file");
            return;
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
			strerror(errno));
		error_box(fe->window, boxmsg);
		return;
	    }
	}

    }
}

static void menu_load_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    char *name, *err;

    name = file_selector(fe, "Enter name of saved game file to load", FALSE);

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
    }
}

static void menu_solve_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    char *msg;

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
    int which = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(menuitem),
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
}

static void menu_about_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    char titlebuf[256];
    char textbuf[1024];

    sprintf(titlebuf, "About %.200s", thegame.name);
    sprintf(textbuf,
	    "%.200s\n\n"
	    "from Simon Tatham's Portable Puzzle Collection\n\n"
	    "%.500s", thegame.name, ver);

    message_box(fe->window, titlebuf, textbuf, TRUE, MB_OK);
}

static GtkWidget *add_menu_item_with_key(frontend *fe, GtkContainer *cont,
                                         char *text, int key)
{
    GtkWidget *menuitem = gtk_menu_item_new_with_label(text);
    int keyqual;
    gtk_container_add(cont, menuitem);
    gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
                        GINT_TO_POINTER(key));
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_key_event), fe);
    switch (key & ~0x1F) {
      case 0x00:
	key += 0x60;
	keyqual = GDK_CONTROL_MASK;
	break;
      case 0x40:
	key += 0x20;
	keyqual = GDK_SHIFT_MASK;
	break;
      default:
	keyqual = 0;
	break;
    }
    gtk_widget_add_accelerator(menuitem,
			       "activate", fe->accelgroup,
			       key, keyqual,
			       GTK_ACCEL_VISIBLE);
    gtk_widget_show(menuitem);
    return menuitem;
}

static void add_menu_separator(GtkContainer *cont)
{
    GtkWidget *menuitem = gtk_menu_item_new();
    gtk_container_add(cont, menuitem);
    gtk_widget_show(menuitem);
}

enum { ARG_EITHER, ARG_SAVE, ARG_ID }; /* for argtype */

static frontend *new_window(char *arg, int argtype, char **error)
{
    frontend *fe;
    GtkBox *vbox, *hbox;
    GtkWidget *menu, *menuitem;
    GdkPixmap *iconpm;
    GList *iconlist;
    int x, y, n;
    char errbuf[1024];
    extern char *const *const xpm_icons[];
    extern const int n_xpm_icons;

    fe = snew(frontend);

    fe->timer_active = FALSE;
    fe->timer_id = -1;

    fe->me = midend_new(fe, &thegame, &gtk_drawing, fe);

    if (arg) {
	char *err;
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
	    return NULL;
	}

    } else {
	midend_new_game(fe->me);
    }

    fe->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(fe->window), thegame.name);

    vbox = GTK_BOX(gtk_vbox_new(FALSE, 0));
    gtk_container_add(GTK_CONTAINER(fe->window), GTK_WIDGET(vbox));
    gtk_widget_show(GTK_WIDGET(vbox));

    fe->accelgroup = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(fe->window), fe->accelgroup);

    hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_box_pack_start(vbox, GTK_WIDGET(hbox), FALSE, FALSE, 0);
    gtk_widget_show(GTK_WIDGET(hbox));

    fe->menubar = gtk_menu_bar_new();
    gtk_box_pack_start(hbox, fe->menubar, TRUE, TRUE, 0);
    gtk_widget_show(fe->menubar);

    menuitem = gtk_menu_item_new_with_mnemonic("_Game");
    gtk_container_add(GTK_CONTAINER(fe->menubar), menuitem);
    gtk_widget_show(menuitem);

    menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);

    add_menu_item_with_key(fe, GTK_CONTAINER(menu), "New", 'n');

    menuitem = gtk_menu_item_new_with_label("Restart");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_restart_event), fe);
    gtk_widget_show(menuitem);

    menuitem = gtk_menu_item_new_with_label("Specific...");
    gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
			GINT_TO_POINTER(CFG_DESC));
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_config_event), fe);
    gtk_widget_show(menuitem);

    menuitem = gtk_menu_item_new_with_label("Random Seed...");
    gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
			GINT_TO_POINTER(CFG_SEED));
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_config_event), fe);
    gtk_widget_show(menuitem);

    fe->preset_radio = NULL;
    fe->preset_custom = NULL;
    fe->n_preset_menu_items = 0;
    fe->preset_threaded = FALSE;
    if ((n = midend_num_presets(fe->me)) > 0 || thegame.can_configure) {
        GtkWidget *submenu;
        int i;

        menuitem = gtk_menu_item_new_with_mnemonic("_Type");
        gtk_container_add(GTK_CONTAINER(fe->menubar), menuitem);
        gtk_widget_show(menuitem);

        submenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

        for (i = 0; i < n; i++) {
            char *name;
            game_params *params;

            midend_fetch_preset(fe->me, i, &name, &params);

	    menuitem =
		gtk_radio_menu_item_new_with_label(fe->preset_radio, name);
	    fe->preset_radio =
		gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(menuitem));
	    fe->n_preset_menu_items++;
            gtk_container_add(GTK_CONTAINER(submenu), menuitem);
            gtk_object_set_data(GTK_OBJECT(menuitem), "user-data", params);
            gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
                               GTK_SIGNAL_FUNC(menu_preset_event), fe);
            gtk_widget_show(menuitem);
        }

	if (thegame.can_configure) {
	    menuitem = fe->preset_custom =
		gtk_radio_menu_item_new_with_label(fe->preset_radio,
						   "Custom...");
	    fe->preset_radio =
		gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(menuitem));
            gtk_container_add(GTK_CONTAINER(submenu), menuitem);
            gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
				GPOINTER_TO_INT(CFG_SETTINGS));
            gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
                               GTK_SIGNAL_FUNC(menu_config_event), fe);
            gtk_widget_show(menuitem);
	}

    }

    add_menu_separator(GTK_CONTAINER(menu));
    menuitem = gtk_menu_item_new_with_label("Load...");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_load_event), fe);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label("Save...");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_save_event), fe);
    gtk_widget_show(menuitem);
#ifndef STYLUS_BASED
    add_menu_separator(GTK_CONTAINER(menu));
    add_menu_item_with_key(fe, GTK_CONTAINER(menu), "Undo", 'u');
    add_menu_item_with_key(fe, GTK_CONTAINER(menu), "Redo", 'r');
#endif
    if (thegame.can_format_as_text_ever) {
	add_menu_separator(GTK_CONTAINER(menu));
	menuitem = gtk_menu_item_new_with_label("Copy");
	gtk_container_add(GTK_CONTAINER(menu), menuitem);
	gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
			   GTK_SIGNAL_FUNC(menu_copy_event), fe);
	gtk_widget_show(menuitem);
	fe->copy_menu_item = menuitem;
    } else {
	fe->copy_menu_item = NULL;
    }
    if (thegame.can_solve) {
	add_menu_separator(GTK_CONTAINER(menu));
	menuitem = gtk_menu_item_new_with_label("Solve");
	gtk_container_add(GTK_CONTAINER(menu), menuitem);
	gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
			   GTK_SIGNAL_FUNC(menu_solve_event), fe);
	gtk_widget_show(menuitem);
    }
    add_menu_separator(GTK_CONTAINER(menu));
    add_menu_item_with_key(fe, GTK_CONTAINER(menu), "Exit", 'q');

    menuitem = gtk_menu_item_new_with_mnemonic("_Help");
    gtk_container_add(GTK_CONTAINER(fe->menubar), menuitem);
    gtk_widget_show(menuitem);

    menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);

    menuitem = gtk_menu_item_new_with_label("About");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_about_event), fe);
    gtk_widget_show(menuitem);

#ifdef STYLUS_BASED
    menuitem=gtk_button_new_with_mnemonic("_Redo");
    gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
			GINT_TO_POINTER((int)('r')));
    gtk_signal_connect(GTK_OBJECT(menuitem), "clicked",
		       GTK_SIGNAL_FUNC(menu_key_event), fe);
    gtk_box_pack_end(hbox, menuitem, FALSE, FALSE, 0);
    gtk_widget_show(menuitem);

    menuitem=gtk_button_new_with_mnemonic("_Undo");
    gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
			GINT_TO_POINTER((int)('u')));
    gtk_signal_connect(GTK_OBJECT(menuitem), "clicked",
		       GTK_SIGNAL_FUNC(menu_key_event), fe);
    gtk_box_pack_end(hbox, menuitem, FALSE, FALSE, 0);
    gtk_widget_show(menuitem);

    if (thegame.flags & REQUIRE_NUMPAD) {
	hbox = GTK_BOX(gtk_hbox_new(FALSE, 0));
	gtk_box_pack_start(vbox, GTK_WIDGET(hbox), FALSE, FALSE, 0);
	gtk_widget_show(GTK_WIDGET(hbox));

	*((int*)errbuf)=0;
	errbuf[1]='\0';
	for(errbuf[0]='0';errbuf[0]<='9';errbuf[0]++) {
	    menuitem=gtk_button_new_with_label(errbuf);
	    gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
				GINT_TO_POINTER((int)(errbuf[0])));
	    gtk_signal_connect(GTK_OBJECT(menuitem), "clicked",
			       GTK_SIGNAL_FUNC(menu_key_event), fe);
	    gtk_box_pack_start(hbox, menuitem, TRUE, TRUE, 0);
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
	gtk_box_pack_end(vbox, viewport, FALSE, FALSE, 0);
	gtk_widget_show(fe->statusbar);
	fe->statusctx = gtk_statusbar_get_context_id
	    (GTK_STATUSBAR(fe->statusbar), "game");
	gtk_statusbar_push(GTK_STATUSBAR(fe->statusbar), fe->statusctx,
			   "test");
	gtk_widget_size_request(fe->statusbar, &req);
#if 0
	/* For GTK 2.0, should we be using gtk_widget_set_size_request? */
#endif
	gtk_widget_set_usize(viewport, -1, req.height);
    } else
	fe->statusbar = NULL;

    fe->area = gtk_drawing_area_new();
#if GTK_CHECK_VERSION(2,0,0)
    GTK_WIDGET_UNSET_FLAGS(fe->area, GTK_DOUBLE_BUFFERED);
#endif
    get_size(fe, &x, &y);
    fe->drawing_area_shrink_pending = FALSE;
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    fe->w = x;
    fe->h = y;

    gtk_box_pack_end(vbox, fe->area, TRUE, TRUE, 0);

    clear_backing_store(fe);
    fe->fonts = NULL;
    fe->nfonts = fe->fontsize = 0;

    fe->paste_data = NULL;
    fe->paste_data_len = 0;

    gtk_signal_connect(GTK_OBJECT(fe->window), "destroy",
		       GTK_SIGNAL_FUNC(destroy), fe);
    gtk_signal_connect(GTK_OBJECT(fe->window), "key_press_event",
		       GTK_SIGNAL_FUNC(key_event), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "button_press_event",
		       GTK_SIGNAL_FUNC(button_event), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "button_release_event",
		       GTK_SIGNAL_FUNC(button_event), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "motion_notify_event",
		       GTK_SIGNAL_FUNC(motion_event), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "selection_get",
		       GTK_SIGNAL_FUNC(selection_get), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "selection_clear_event",
		       GTK_SIGNAL_FUNC(selection_clear), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "expose_event",
		       GTK_SIGNAL_FUNC(expose_area), fe);
    gtk_signal_connect(GTK_OBJECT(fe->window), "map_event",
		       GTK_SIGNAL_FUNC(map_window), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "configure_event",
		       GTK_SIGNAL_FUNC(configure_area), fe);
    gtk_signal_connect(GTK_OBJECT(fe->window), "configure_event",
		       GTK_SIGNAL_FUNC(configure_window), fe);

    gtk_widget_add_events(GTK_WIDGET(fe->area),
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
			  GDK_BUTTON_MOTION_MASK |
			  GDK_POINTER_MOTION_HINT_MASK);

    if (n_xpm_icons) {
	gtk_widget_realize(fe->window);
	iconpm = gdk_pixmap_create_from_xpm_d(fe->window->window, NULL,
					      NULL, (gchar **)xpm_icons[0]);
	gdk_window_set_icon(fe->window->window, NULL, iconpm, NULL);
	iconlist = NULL;
	for (n = 0; n < n_xpm_icons; n++) {
	    iconlist =
		g_list_append(iconlist,
			      gdk_pixbuf_new_from_xpm_data((const gchar **)
							   xpm_icons[n]));
	}
	gdk_window_set_icon_list(fe->window->window, iconlist);
    }

    gtk_widget_show(fe->area);
    gtk_widget_show(fe->window);

    fe->drawing_area_shrink_pending = TRUE;
    try_shrink_drawing_area(fe);
    set_window_background(fe, 0);

    return fe;
}

char *fgetline(FILE *fp)
{
    char *ret = snewn(512, char);
    int size = 512, len = 0;
    while (fgets(ret + len, size - len, fp)) {
	len += strlen(ret + len);
	if (ret[len-1] == '\n')
	    break;		       /* got a newline, we're done */
	size = len + 512;
	ret = sresize(ret, size, char);
    }
    if (len == 0) {		       /* first fgets returned NULL */
	sfree(ret);
	return NULL;
    }
    ret[len] = '\0';
    return ret;
}

int main(int argc, char **argv)
{
    char *pname = argv[0];
    char *error;
    int ngenerate = 0, print = FALSE, px = 1, py = 1;
    int soln = FALSE, colour = FALSE;
    float scale = 1.0F;
    float redo_proportion = 0.0F;
    char *savefile = NULL, *savesuffix = NULL;
    char *arg = NULL;
    int argtype = ARG_EITHER;
    char *screenshot_file = NULL;
    int doing_opts = TRUE;
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
	    print = TRUE;
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
	    soln = TRUE;
	} else if (doing_opts && !strcmp(p, "--colour")) {
	    if (!thegame.can_print_in_colour) {
		fprintf(stderr, "%s: this game does not support colour"
			" printing\n", pname);
		return 1;
	    }
	    colour = TRUE;
	} else if (doing_opts && !strcmp(p, "--load")) {
	    argtype = ARG_SAVE;
	} else if (doing_opts && !strcmp(p, "--game")) {
	    argtype = ARG_ID;
	} else if (doing_opts && !strcmp(p, "--")) {
	    doing_opts = FALSE;
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

    if (*errbuf) {
	fputs(errbuf, stderr);
	return 1;
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
	    char *pstr, *err;

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
	    sfree(pstr);

	    midend_new_game(me);

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
                    char *err = midend_solve(me);
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
	    if (!doc && !savefile) {
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
    } else {
	frontend *fe;

	gtk_init(&argc, &argv);

	fe = new_window(arg, argtype, &error);

	if (!fe) {
	    fprintf(stderr, "%s: %s\n", pname, error);
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
	    midend_process_key(fe->me, 0, 0, 'r');
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
