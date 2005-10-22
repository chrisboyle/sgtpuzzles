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

#include <sys/time.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "puzzles.h"

#if GTK_CHECK_VERSION(2,0,0)
#define USE_PANGO
#endif

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
    GtkWidget *area;
    GtkWidget *statusbar;
    guint statusctx;
    GdkPixmap *pixmap;
    GdkColor *colours;
    int ncolours;
    GdkColormap *colmap;
    int w, h;
    midend *me;
    GdkGC *gc;
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
    char *filesel_name;
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

void gtk_start_draw(void *handle)
{
    frontend *fe = (frontend *)handle;
    fe->gc = gdk_gc_new(fe->area->window);
    fe->bbox_l = fe->w;
    fe->bbox_r = 0;
    fe->bbox_u = fe->h;
    fe->bbox_d = 0;
}

void gtk_clip(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    GdkRectangle rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;

    gdk_gc_set_clip_rectangle(fe->gc, &rect);
}

void gtk_unclip(void *handle)
{
    frontend *fe = (frontend *)handle;
    GdkRectangle rect;

    rect.x = 0;
    rect.y = 0;
    rect.width = fe->w;
    rect.height = fe->h;

    gdk_gc_set_clip_rectangle(fe->gc, &rect);
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

#ifdef USE_PANGO
        /*
         * Use Pango to find the closest match to the requested
         * font.
         */
        {
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
            fe->fonts[i].desc = fd;
        }

#else
	/*
	 * In GTK 1.2, I don't know of any plausible way to
	 * pick a suitable font, so I'm just going to be
	 * tedious.
	 */
	fe->fonts[i].font = gdk_font_load(fonttype == FONT_FIXED ?
					  "fixed" : "variable");
#endif

    }

    /*
     * Set the colour.
     */
    gdk_gc_set_foreground(fe->gc, &fe->colours[colour]);

#ifdef USE_PANGO

    {
	PangoLayout *layout;
	PangoRectangle rect;

	/*
	 * Create a layout.
	 */
	layout = pango_layout_new(gtk_widget_get_pango_context(fe->area));
	pango_layout_set_font_description(layout, fe->fonts[i].desc);
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

	gdk_draw_layout(fe->pixmap, fe->gc, rect.x + x, rect.y + y, layout);

	g_object_unref(layout);
    }

#else
    /*
     * Find string dimensions and process alignment.
     */
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

    }

    /*
     * Actually draw the text.
     */
    gdk_draw_string(fe->pixmap, fe->fonts[i].font, fe->gc, x, y, text);
#endif

}

void gtk_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
    frontend *fe = (frontend *)handle;
    gdk_gc_set_foreground(fe->gc, &fe->colours[colour]);
    gdk_draw_rectangle(fe->pixmap, fe->gc, 1, x, y, w, h);
}

void gtk_draw_line(void *handle, int x1, int y1, int x2, int y2, int colour)
{
    frontend *fe = (frontend *)handle;
    gdk_gc_set_foreground(fe->gc, &fe->colours[colour]);
    gdk_draw_line(fe->pixmap, fe->gc, x1, y1, x2, y2);
}

void gtk_draw_poly(void *handle, int *coords, int npoints,
		   int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    GdkPoint *points = snewn(npoints, GdkPoint);
    int i;

    for (i = 0; i < npoints; i++) {
        points[i].x = coords[i*2];
        points[i].y = coords[i*2+1];
    }

    if (fillcolour >= 0) {
	gdk_gc_set_foreground(fe->gc, &fe->colours[fillcolour]);
	gdk_draw_polygon(fe->pixmap, fe->gc, TRUE, points, npoints);
    }
    assert(outlinecolour >= 0);
    gdk_gc_set_foreground(fe->gc, &fe->colours[outlinecolour]);

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

void gtk_draw_circle(void *handle, int cx, int cy, int radius,
		     int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    if (fillcolour >= 0) {
	gdk_gc_set_foreground(fe->gc, &fe->colours[fillcolour]);
	gdk_draw_arc(fe->pixmap, fe->gc, TRUE,
		     cx - radius, cy - radius,
		     2 * radius, 2 * radius, 0, 360 * 64);
    }

    assert(outlinecolour >= 0);
    gdk_gc_set_foreground(fe->gc, &fe->colours[outlinecolour]);
    gdk_draw_arc(fe->pixmap, fe->gc, FALSE,
		 cx - radius, cy - radius,
		 2 * radius, 2 * radius, 0, 360 * 64);
}

struct blitter {
    GdkPixmap *pixmap;
    int w, h, x, y;
};

blitter *gtk_blitter_new(void *handle, int w, int h)
{
    /*
     * We can't create the pixmap right now, because fe->window
     * might not yet exist. So we just cache w and h and create it
     * during the firs call to blitter_save.
     */
    blitter *bl = snew(blitter);
    bl->pixmap = NULL;
    bl->w = w;
    bl->h = h;
    return bl;
}

void gtk_blitter_free(void *handle, blitter *bl)
{
    if (bl->pixmap)
        gdk_pixmap_unref(bl->pixmap);
    sfree(bl);
}

void gtk_blitter_save(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    if (!bl->pixmap)
        bl->pixmap = gdk_pixmap_new(fe->area->window, bl->w, bl->h, -1);
    bl->x = x;
    bl->y = y;
    gdk_draw_pixmap(bl->pixmap,
                    fe->area->style->fg_gc[GTK_WIDGET_STATE(fe->area)],
                    fe->pixmap,
                    x, y, 0, 0, bl->w, bl->h);
}

void gtk_blitter_load(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    assert(bl->pixmap);
    if (x == BLITTER_FROMSAVED && y == BLITTER_FROMSAVED) {
        x = bl->x;
        y = bl->y;
    }
    gdk_draw_pixmap(fe->pixmap,
                    fe->area->style->fg_gc[GTK_WIDGET_STATE(fe->area)],
                    bl->pixmap,
                    0, 0, x, y, bl->w, bl->h);
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
    gdk_gc_unref(fe->gc);
    fe->gc = NULL;

    if (fe->bbox_l < fe->bbox_r && fe->bbox_u < fe->bbox_d) {
	gdk_draw_pixmap(fe->area->window,
			fe->area->style->fg_gc[GTK_WIDGET_STATE(fe->area)],
			fe->pixmap,
                        fe->bbox_l, fe->bbox_u,
                        fe->ox + fe->bbox_l, fe->oy + fe->bbox_u,
                        fe->bbox_r - fe->bbox_l, fe->bbox_d - fe->bbox_u);
    }
}

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
    NULL,			       /* line_width */
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

    if (!fe->pixmap)
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

    if (!fe->pixmap)
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

    if (!fe->pixmap)
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

    return TRUE;
}

static gint expose_area(GtkWidget *widget, GdkEventExpose *event,
                        gpointer data)
{
    frontend *fe = (frontend *)data;

    if (fe->pixmap) {
	gdk_draw_pixmap(widget->window,
			widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
			fe->pixmap,
			event->area.x - fe->ox, event->area.y - fe->oy,
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
    GdkGC *gc;
    int x, y;

    if (fe->pixmap)
        gdk_pixmap_unref(fe->pixmap);

    x = fe->w = event->width;
    y = fe->h = event->height;
    midend_size(fe->me, &x, &y, TRUE);
    fe->pw = x;
    fe->ph = y;
    fe->ox = (fe->w - fe->pw) / 2;
    fe->oy = (fe->h - fe->ph) / 2;

    fe->pixmap = gdk_pixmap_new(widget->window, fe->pw, fe->ph, -1);

    gc = gdk_gc_new(fe->area->window);
    gdk_gc_set_foreground(gc, &fe->colours[0]);
    gdk_draw_rectangle(fe->pixmap, gc, 1, 0, 0, fe->pw, fe->ph);
    gdk_gc_unref(gc);

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
	titles = "OK\0";
	def = cancel = 0;
    } else {
	assert(type == MB_YESNO);
	titles = "Yes\0No\0";
	def = 0;
	cancel = 1;
    }
    i = 0;
    
    while (*titles) {
	button = gtk_button_new_with_label(titles);
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
    return (type == MB_YESNO ? i == 0 : TRUE);
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

    w = gtk_button_new_with_label("OK");
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(fe->cfgbox)->action_area),
                     w, FALSE, FALSE, 0);
    gtk_widget_show(w);
    GTK_WIDGET_SET_FLAGS(w, GTK_CAN_DEFAULT);
    gtk_window_set_default(GTK_WINDOW(fe->cfgbox), w);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
                       GTK_SIGNAL_FUNC(config_ok_button_clicked), fe);

    w = gtk_button_new_with_label("Cancel");
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(fe->cfgbox)->action_area),
                     w, FALSE, FALSE, 0);
    gtk_widget_show(w);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
                       GTK_SIGNAL_FUNC(config_cancel_button_clicked), fe);
    cancel = w;

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
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
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
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
			     GTK_EXPAND | GTK_SHRINK | GTK_FILL,
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

static void menu_preset_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    game_params *params =
        (game_params *)gtk_object_get_data(GTK_OBJECT(menuitem), "user-data");
    int x, y;

    midend_set_params(fe->me, params);
    midend_new_game(fe->me);
    get_size(fe, &x, &y);
    fe->w = x;
    fe->h = y;
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    {
        GtkRequisition req;
        gtk_widget_size_request(GTK_WIDGET(fe->window), &req);
        gtk_window_resize(GTK_WINDOW(fe->window), req.width, req.height);
    }
}

GdkAtom compound_text_atom, utf8_string_atom;
int paste_initialised = FALSE;

void init_paste()
{
    unsigned char empty[] = { 0 };

    if (paste_initialised)
	return;

    if (!compound_text_atom)
        compound_text_atom = gdk_atom_intern("COMPOUND_TEXT", FALSE);
    if (!utf8_string_atom)
        utf8_string_atom = gdk_atom_intern("UTF8_STRING", FALSE);

    /*
     * Ensure that all the cut buffers exist - according to the
     * ICCCM, we must do this before we start using cut buffers.
     */
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER0, XA_STRING, 8, PropModeAppend, empty, 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER1, XA_STRING, 8, PropModeAppend, empty, 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER2, XA_STRING, 8, PropModeAppend, empty, 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER3, XA_STRING, 8, PropModeAppend, empty, 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER4, XA_STRING, 8, PropModeAppend, empty, 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER5, XA_STRING, 8, PropModeAppend, empty, 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER6, XA_STRING, 8, PropModeAppend, empty, 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER7, XA_STRING, 8, PropModeAppend, empty, 0);
}

/* Store data in a cut-buffer. */
void store_cutbuffer(char *ptr, int len)
{
    /* ICCCM says we must rotate the buffers before storing to buffer 0. */
    XRotateBuffers(GDK_DISPLAY(), 1);
    XStoreBytes(GDK_DISPLAY(), ptr, len);
}

void write_clip(frontend *fe, char *data)
{
    init_paste();

    if (fe->paste_data)
	sfree(fe->paste_data);

    /*
     * For this simple application we can safely assume that the
     * data passed to this function is pure ASCII, which means we
     * can return precisely the same stuff for types STRING,
     * COMPOUND_TEXT or UTF8_STRING.
     */

    fe->paste_data = data;
    fe->paste_data_len = strlen(data);

    store_cutbuffer(fe->paste_data, fe->paste_data_len);

    if (gtk_selection_owner_set(fe->area, GDK_SELECTION_PRIMARY,
				CurrentTime)) {
	gtk_selection_add_target(fe->area, GDK_SELECTION_PRIMARY,
				 GDK_SELECTION_TYPE_STRING, 1);
	gtk_selection_add_target(fe->area, GDK_SELECTION_PRIMARY,
				 compound_text_atom, 1);
	gtk_selection_add_target(fe->area, GDK_SELECTION_PRIMARY,
				 utf8_string_atom, 1);
    }
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

static void savefile_write(void *wctx, void *buf, int len)
{
    FILE *fp = (FILE *)wctx;
    fwrite(buf, 1, len, fp);
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

        midend_serialise(fe->me, savefile_write, fp);

        fclose(fp);
    }
}

static void menu_load_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    char *name, *err;
    int x, y;

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

        get_size(fe, &x, &y);
        fe->w = x;
        fe->h = y;
        gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
        {
            GtkRequisition req;
            gtk_widget_size_request(GTK_WIDGET(fe->window), &req);
            gtk_window_resize(GTK_WINDOW(fe->window), req.width, req.height);
        }

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
    int x, y;

    if (!get_config(fe, which))
	return;

    midend_new_game(fe->me);
    get_size(fe, &x, &y);
    fe->w = x;
    fe->h = y;
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    {
        GtkRequisition req;
        gtk_widget_size_request(GTK_WIDGET(fe->window), &req);
        gtk_window_resize(GTK_WINDOW(fe->window), req.width, req.height);
    }
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
    gtk_container_add(cont, menuitem);
    gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
                        GINT_TO_POINTER(key));
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_key_event), fe);
    gtk_widget_show(menuitem);
    return menuitem;
}

static void add_menu_separator(GtkContainer *cont)
{
    GtkWidget *menuitem = gtk_menu_item_new();
    gtk_container_add(cont, menuitem);
    gtk_widget_show(menuitem);
}

static frontend *new_window(char *arg, char **error)
{
    frontend *fe;
    GtkBox *vbox;
    GtkWidget *menubar, *menu, *menuitem;
    int x, y, n;
    char errbuf[1024];

    fe = snew(frontend);

    fe->timer_active = FALSE;
    fe->timer_id = -1;

    fe->me = midend_new(fe, &thegame, &gtk_drawing, fe);

    if (arg) {
	char *err;

	errbuf[0] = '\0';

	/*
	 * Try treating the argument as a game ID.
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
#if 0
    gtk_window_set_resizable(GTK_WINDOW(fe->window), FALSE);
#else
    gtk_window_set_policy(GTK_WINDOW(fe->window), FALSE, FALSE, TRUE);
#endif
    vbox = GTK_BOX(gtk_vbox_new(FALSE, 0));
    gtk_container_add(GTK_CONTAINER(fe->window), GTK_WIDGET(vbox));
    gtk_widget_show(GTK_WIDGET(vbox));

    menubar = gtk_menu_bar_new();
    gtk_box_pack_start(vbox, menubar, FALSE, FALSE, 0);
    gtk_widget_show(menubar);

    menuitem = gtk_menu_item_new_with_label("Game");
    gtk_container_add(GTK_CONTAINER(menubar), menuitem);
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

    if ((n = midend_num_presets(fe->me)) > 0 || thegame.can_configure) {
        GtkWidget *submenu;
        int i;

        menuitem = gtk_menu_item_new_with_label("Type");
        gtk_container_add(GTK_CONTAINER(menubar), menuitem);
        gtk_widget_show(menuitem);

        submenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

        for (i = 0; i < n; i++) {
            char *name;
            game_params *params;

            midend_fetch_preset(fe->me, i, &name, &params);

            menuitem = gtk_menu_item_new_with_label(name);
            gtk_container_add(GTK_CONTAINER(submenu), menuitem);
            gtk_object_set_data(GTK_OBJECT(menuitem), "user-data", params);
            gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
                               GTK_SIGNAL_FUNC(menu_preset_event), fe);
            gtk_widget_show(menuitem);
        }

	if (thegame.can_configure) {
            menuitem = gtk_menu_item_new_with_label("Custom...");
            gtk_object_set_data(GTK_OBJECT(menuitem), "user-data",
				GPOINTER_TO_INT(CFG_SETTINGS));
            gtk_container_add(GTK_CONTAINER(submenu), menuitem);
            gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
                               GTK_SIGNAL_FUNC(menu_config_event), fe);
            gtk_widget_show(menuitem);
	}
    }

    add_menu_separator(GTK_CONTAINER(menu));
    menuitem = gtk_menu_item_new_with_label("Load");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_load_event), fe);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label("Save");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_save_event), fe);
    gtk_widget_show(menuitem);
    add_menu_separator(GTK_CONTAINER(menu));
    add_menu_item_with_key(fe, GTK_CONTAINER(menu), "Undo", 'u');
    add_menu_item_with_key(fe, GTK_CONTAINER(menu), "Redo", '\x12');
    if (thegame.can_format_as_text) {
	add_menu_separator(GTK_CONTAINER(menu));
	menuitem = gtk_menu_item_new_with_label("Copy");
	gtk_container_add(GTK_CONTAINER(menu), menuitem);
	gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
			   GTK_SIGNAL_FUNC(menu_copy_event), fe);
	gtk_widget_show(menuitem);
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

    menuitem = gtk_menu_item_new_with_label("Help");
    gtk_container_add(GTK_CONTAINER(menubar), menuitem);
    gtk_widget_show(menuitem);

    menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), menu);

    menuitem = gtk_menu_item_new_with_label("About");
    gtk_container_add(GTK_CONTAINER(menu), menuitem);
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(menu_about_event), fe);
    gtk_widget_show(menuitem);

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
            if (!success[i])
                g_error("couldn't allocate colour %d (#%02x%02x%02x)\n",
                        i, fe->colours[i].red >> 8,
                        fe->colours[i].green >> 8,
                        fe->colours[i].blue >> 8);
        }
    }

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
    get_size(fe, &x, &y);
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    fe->w = x;
    fe->h = y;

    gtk_box_pack_end(vbox, fe->area, TRUE, TRUE, 0);

    fe->pixmap = NULL;
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

    gtk_widget_add_events(GTK_WIDGET(fe->area),
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
			  GDK_BUTTON_MOTION_MASK);

    gtk_widget_show(fe->area);
    gtk_widget_show(fe->window);

    gdk_window_set_background(fe->area->window, &fe->colours[0]);
    gdk_window_set_background(fe->window->window, &fe->colours[0]);

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
    char *arg = NULL;
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
    if (ngenerate > 0 || print) {
	int i, n = 1;
	midend *me;
	char *id;
	document *doc = NULL;

	if (*errbuf) {
	    fputs(errbuf, stderr);
	    return 1;
	}

	n = ngenerate;

	me = midend_new(NULL, &thegame, NULL, NULL);
	i = 0;

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
	    } else {
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

	gtk_init(&argc, &argv);

	if (!new_window(argc > 1 ? argv[1] : NULL, &error)) {
	    fprintf(stderr, "%s: %s\n", pname, error);
	    return 1;
	}

	gtk_main();
    }

    return 0;
}
