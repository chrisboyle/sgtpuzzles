/*
 * gtk.c: GTK front end for my puzzle collection.
 */

#include <stdio.h>
#include <stdlib.h>
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
struct window_data {
    GtkWidget *window;
    GtkWidget *area;
    midend_data *me;
};

static void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct window_data *wdata = (struct window_data *)data;

    IGNORE(wdata);

    if (!midend_process_key(wdata->me, 0, 0, event->keyval))
	gtk_widget_destroy(wdata->window);

    return TRUE;
}

gint button_event(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    struct window_data *wdata = (struct window_data *)data;

    IGNORE(wdata);

    return TRUE;
}

static struct window_data *new_window(void)
{
    struct window_data *wdata;
    int x, y;

    wdata = snew(struct window_data);

    wdata->me = midend_new();
    midend_new_game(wdata->me, NULL);

    wdata->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    wdata->area = gtk_drawing_area_new();
    midend_size(wdata->me, &x, &y);
    gtk_drawing_area_size(GTK_DRAWING_AREA(wdata->area), x, y);

    gtk_container_add(GTK_CONTAINER(wdata->window), wdata->area);
    gtk_widget_show(wdata->area);

    gtk_signal_connect(GTK_OBJECT(wdata->window), "destroy",
		       GTK_SIGNAL_FUNC(destroy), wdata);
    gtk_signal_connect(GTK_OBJECT(wdata->window), "key_press_event",
		       GTK_SIGNAL_FUNC(key_event), wdata);
    gtk_signal_connect(GTK_OBJECT(wdata->area), "button_press_event",
		       GTK_SIGNAL_FUNC(button_event), wdata);
    gtk_widget_show(wdata->window);
    return wdata;
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    (void) new_window();
    gtk_main();

    return 0;
}
