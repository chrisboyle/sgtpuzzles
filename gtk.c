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
};

static void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

static struct window_data *new_window(void)
{
    struct window_data *wdata;

    wdata = snew(struct window_data);

    wdata->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_signal_connect(GTK_OBJECT(wdata->window), "destroy",
		       GTK_SIGNAL_FUNC(destroy), wdata);
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
