/*
 * gtk.c: GTK front end for my puzzle collection.
 * 
 * TODO:
 * 
 *  - Handle resizing, probably just by forbidding it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>

#include <gtk/gtk.h>

#include "puzzles.h"

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
    GdkPixmap *pixmap;
    GdkColor *colours;
    int ncolours;
    GdkColormap *colmap;
    int w, h;
    midend_data *me;
    GdkGC *gc;
    int bbox_l, bbox_r, bbox_u, bbox_d;
    int timer_active;
};

void frontend_default_colour(frontend *fe, float *output)
{
    GdkColor col = fe->window->style->bg[GTK_STATE_NORMAL];
    output[0] = col.red / 65535.0;
    output[1] = col.green / 65535.0;
    output[2] = col.blue / 65535.0;
}

void start_draw(frontend *fe)
{
    fe->gc = gdk_gc_new(fe->area->window);
    fe->bbox_l = fe->w;
    fe->bbox_r = 0;
    fe->bbox_u = fe->h;
    fe->bbox_d = 0;
}

void draw_rect(frontend *fe, int x, int y, int w, int h, int colour)
{
    gdk_gc_set_foreground(fe->gc, &fe->colours[colour]);
    gdk_draw_rectangle(fe->pixmap, fe->gc, 1, x, y, w, h);
}

void draw_line(frontend *fe, int x1, int y1, int x2, int y2, int colour)
{
    gdk_gc_set_foreground(fe->gc, &fe->colours[colour]);
    gdk_draw_line(fe->pixmap, fe->gc, x1, y1, x2, y2);
}

void draw_polygon(frontend *fe, int *coords, int npoints,
                  int fill, int colour)
{
    GdkPoint *points = snewn(npoints, GdkPoint);
    int i;

    for (i = 0; i < npoints; i++) {
        points[i].x = coords[i*2];
        points[i].y = coords[i*2+1];
    }

    gdk_gc_set_foreground(fe->gc, &fe->colours[colour]);
    gdk_draw_polygon(fe->pixmap, fe->gc, fill, points, npoints);

    sfree(points);
}

void draw_update(frontend *fe, int x, int y, int w, int h)
{
    if (fe->bbox_l > x  ) fe->bbox_l = x  ;
    if (fe->bbox_r < x+w) fe->bbox_r = x+w;
    if (fe->bbox_u > y  ) fe->bbox_u = y  ;
    if (fe->bbox_d < y+h) fe->bbox_d = y+h;
}

void end_draw(frontend *fe)
{
    gdk_gc_unref(fe->gc);
    fe->gc = NULL;

    if (fe->bbox_l < fe->bbox_r && fe->bbox_u < fe->bbox_d) {
	gdk_draw_pixmap(fe->area->window,
			fe->area->style->fg_gc[GTK_WIDGET_STATE(fe->area)],
			fe->pixmap,
                        fe->bbox_l, fe->bbox_u,
                        fe->bbox_l, fe->bbox_u,
                        fe->bbox_r - fe->bbox_l, fe->bbox_d - fe->bbox_u);
    }
}

static void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

static gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    frontend *fe = (frontend *)data;

    if (!fe->pixmap)
        return TRUE;

    if (event->string[0] && !event->string[1] &&
        !midend_process_key(fe->me, 0, 0, event->string[0]))
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

    if (event->type != GDK_BUTTON_PRESS)
        return TRUE;

    if (event->button == 1)
	button = LEFT_BUTTON;
    else if (event->button == 2)
	button = MIDDLE_BUTTON;
    else if (event->button == 3)
	button = RIGHT_BUTTON;
    else
	return FALSE;		       /* don't even know what button! */

    if (!midend_process_key(fe->me, event->x, event->y, button))
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
			event->area.x, event->area.y,
			event->area.x, event->area.y,
			event->area.width, event->area.height);
    }
    return TRUE;
}

static gint configure_area(GtkWidget *widget,
                           GdkEventConfigure *event, gpointer data)
{
    frontend *fe = (frontend *)data;
    GdkGC *gc;

    fe->pixmap = gdk_pixmap_new(widget->window, fe->w, fe->h, -1);

    gc = gdk_gc_new(fe->area->window);
    gdk_gc_set_foreground(gc, &fe->colours[0]);
    gdk_draw_rectangle(fe->pixmap, gc, 1, 0, 0, fe->w, fe->h);
    gdk_gc_unref(gc);

    midend_redraw(fe->me);

    return TRUE;
}

static gint timer_func(gpointer data)
{
    frontend *fe = (frontend *)data;

    if (fe->timer_active)
        midend_timer(fe->me, 0.02);    /* may clear timer_active */

    return fe->timer_active;
}

void deactivate_timer(frontend *fe)
{
    fe->timer_active = FALSE;
}

void activate_timer(frontend *fe)
{
    gtk_timeout_add(20, timer_func, fe);
    fe->timer_active = TRUE;
}

static frontend *new_window(void)
{
    frontend *fe;
    int x, y;

    fe = snew(frontend);

    fe->me = midend_new(fe);
    midend_new_game(fe->me, NULL);

    fe->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

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

    fe->area = gtk_drawing_area_new();
    midend_size(fe->me, &x, &y);
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    fe->w = x;
    fe->h = y;

    gtk_container_add(GTK_CONTAINER(fe->window), fe->area);

    fe->pixmap = NULL;

    gtk_signal_connect(GTK_OBJECT(fe->window), "destroy",
		       GTK_SIGNAL_FUNC(destroy), fe);
    gtk_signal_connect(GTK_OBJECT(fe->window), "key_press_event",
		       GTK_SIGNAL_FUNC(key_event), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "button_press_event",
		       GTK_SIGNAL_FUNC(button_event), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "expose_event",
		       GTK_SIGNAL_FUNC(expose_area), fe);
    gtk_signal_connect(GTK_OBJECT(fe->area), "configure_event",
		       GTK_SIGNAL_FUNC(configure_area), fe);

    gtk_widget_add_events(GTK_WIDGET(fe->area), GDK_BUTTON_PRESS_MASK);

    gtk_widget_show(fe->area);
    gtk_widget_show(fe->window);

    return fe;
}

int main(int argc, char **argv)
{
    srand(time(NULL));

    gtk_init(&argc, &argv);
    (void) new_window();
    gtk_main();

    return 0;
}
