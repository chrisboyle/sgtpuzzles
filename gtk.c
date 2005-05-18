/*
 * gtk.c: GTK front end for my puzzle collection.
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#include <sys/time.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

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

struct font {
    GdkFont *font;
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
    midend_data *me;
    GdkGC *gc;
    int bbox_l, bbox_r, bbox_u, bbox_d;
    int timer_active, timer_id;
    struct timeval last_time;
    struct font *fonts;
    int nfonts, fontsize;
    config_item *cfg;
    int cfg_which, cfgret;
    GtkWidget *cfgbox;
    char *paste_data;
    int paste_data_len;
};

void get_random_seed(void **randseed, int *randseedsize)
{
    time_t *tp = snew(time_t);
    time(tp);
    *randseed = (void *)tp;
    *randseedsize = sizeof(time_t);
}

void frontend_default_colour(frontend *fe, float *output)
{
    GdkColor col = fe->window->style->bg[GTK_STATE_NORMAL];
    output[0] = col.red / 65535.0;
    output[1] = col.green / 65535.0;
    output[2] = col.blue / 65535.0;
}

void status_bar(frontend *fe, char *text)
{
    assert(fe->statusbar);

    gtk_statusbar_pop(GTK_STATUSBAR(fe->statusbar), fe->statusctx);
    gtk_statusbar_push(GTK_STATUSBAR(fe->statusbar), fe->statusctx, text);
}

void start_draw(frontend *fe)
{
    fe->gc = gdk_gc_new(fe->area->window);
    fe->bbox_l = fe->w;
    fe->bbox_r = 0;
    fe->bbox_u = fe->h;
    fe->bbox_d = 0;
}

void clip(frontend *fe, int x, int y, int w, int h)
{
    GdkRectangle rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;

    gdk_gc_set_clip_rectangle(fe->gc, &rect);
}

void unclip(frontend *fe)
{
    GdkRectangle rect;

    rect.x = 0;
    rect.y = 0;
    rect.width = fe->w;
    rect.height = fe->h;

    gdk_gc_set_clip_rectangle(fe->gc, &rect);
}

void draw_text(frontend *fe, int x, int y, int fonttype, int fontsize,
               int align, int colour, char *text)
{
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

#if GTK_CHECK_VERSION(2,0,0)
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
            fe->fonts[i].font = gdk_font_from_description(fd);
            pango_font_description_free(fd);
        }

        if (!fe->fonts[i].font)
#endif
            /*
             * In GTK 1.2, I don't know of any plausible way to
             * pick a suitable font, so I'm just going to be
             * tedious.
             * 
             * This is also fallback code called if the Pango
             * approach fails to find an appropriate font.
             */
            fe->fonts[i].font = gdk_font_load(fonttype == FONT_FIXED ?
                                              "fixed" : "variable");
    }

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
     * Set colour and actually draw text.
     */
    gdk_gc_set_foreground(fe->gc, &fe->colours[colour]);
    gdk_draw_string(fe->pixmap, fe->fonts[i].font, fe->gc, x, y, text);
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
    frontend *fe = (frontend *)data;
    deactivate_timer(fe);
    gtk_main_quit();
}

static gint key_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    frontend *fe = (frontend *)data;
    int keyval;

    if (!fe->pixmap)
        return TRUE;

    if (event->keyval == GDK_Up)
        keyval = CURSOR_UP;
    else if (event->keyval == GDK_KP_Up || event->keyval == GDK_KP_8)
	keyval = MOD_NUM_KEYPAD | '8';
    else if (event->keyval == GDK_Down)
        keyval = CURSOR_DOWN;
    else if (event->keyval == GDK_KP_Down || event->keyval == GDK_KP_2)
	keyval = MOD_NUM_KEYPAD | '2';
    else if (event->keyval == GDK_Left)
        keyval = CURSOR_LEFT;
    else if (event->keyval == GDK_KP_Left || event->keyval == GDK_KP_4)
	keyval = MOD_NUM_KEYPAD | '4';
    else if (event->keyval == GDK_Right)
        keyval = CURSOR_RIGHT;
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
    else if (event->button == 1)
	button = LEFT_BUTTON;
    else if (event->button == 3)
	button = RIGHT_BUTTON;
    else
	return FALSE;		       /* don't even know what button! */

    if (event->type == GDK_BUTTON_RELEASE)
        button += LEFT_RELEASE - LEFT_BUTTON;

    if (!midend_process_key(fe->me, event->x, event->y, button))
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

    if (fe->pixmap)
        gdk_pixmap_unref(fe->pixmap);

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
        gtk_timeout_remove(fe->timer_id);
    fe->timer_active = FALSE;
}

void activate_timer(frontend *fe)
{
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

static void errmsg_button_clicked(GtkButton *button, gpointer data)
{
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

void message_box(GtkWidget *parent, char *title, char *msg, int centre)
{
    GtkWidget *window, *hbox, *text, *ok;

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
    ok = gtk_button_new_with_label("OK");
    gtk_box_pack_end(GTK_BOX(GTK_DIALOG(window)->action_area),
                     ok, FALSE, FALSE, 0);
    gtk_widget_show(ok);
    GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
    gtk_window_set_default(GTK_WINDOW(window), ok);
    gtk_signal_connect(GTK_OBJECT(ok), "clicked",
                       GTK_SIGNAL_FUNC(errmsg_button_clicked), window);
    gtk_signal_connect(GTK_OBJECT(window), "destroy",
                       GTK_SIGNAL_FUNC(window_destroy), NULL);
    gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
		       GTK_SIGNAL_FUNC(win_key_press), ok);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent));
    //set_transient_window_pos(parent, window);
    gtk_widget_show(window);
    gtk_main();
}

void error_box(GtkWidget *parent, char *msg)
{
    message_box(parent, "Error", msg, FALSE);
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
    //set_transient_window_pos(fe->window, fe->cfgbox);
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

static void menu_preset_event(GtkMenuItem *menuitem, gpointer data)
{
    frontend *fe = (frontend *)data;
    game_params *params =
        (game_params *)gtk_object_get_data(GTK_OBJECT(menuitem), "user-data");
    int x, y;

    midend_set_params(fe->me, params);
    midend_new_game(fe->me);
    midend_size(fe->me, &x, &y);
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    fe->w = x;
    fe->h = y;
}

GdkAtom compound_text_atom, utf8_string_atom;
int paste_initialised = FALSE;

void init_paste()
{
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
		    XA_CUT_BUFFER0, XA_STRING, 8, PropModeAppend, "", 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER1, XA_STRING, 8, PropModeAppend, "", 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER2, XA_STRING, 8, PropModeAppend, "", 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER3, XA_STRING, 8, PropModeAppend, "", 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER4, XA_STRING, 8, PropModeAppend, "", 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER5, XA_STRING, 8, PropModeAppend, "", 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER6, XA_STRING, 8, PropModeAppend, "", 0);
    XChangeProperty(GDK_DISPLAY(), GDK_ROOT_WINDOW(),
		    XA_CUT_BUFFER7, XA_STRING, 8, PropModeAppend, "", 0);
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
    midend_size(fe->me, &x, &y);
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    fe->w = x;
    fe->h = y;
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

    message_box(fe->window, titlebuf, textbuf, TRUE);
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

static frontend *new_window(char *game_id, char **error)
{
    frontend *fe;
    GtkBox *vbox;
    GtkWidget *menubar, *menu, *menuitem;
    int x, y, n;

    fe = snew(frontend);

    fe->me = midend_new(fe, &thegame);
    if (game_id) {
        *error = midend_game_id(fe->me, game_id);
        if (*error) {
            midend_free(fe->me);
            sfree(fe);
            return NULL;
        }
    }
    midend_new_game(fe->me);

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
    midend_size(fe->me, &x, &y);
    gtk_drawing_area_size(GTK_DRAWING_AREA(fe->area), x, y);
    fe->w = x;
    fe->h = y;

    gtk_box_pack_end(vbox, fe->area, FALSE, FALSE, 0);

    fe->pixmap = NULL;
    fe->fonts = NULL;
    fe->nfonts = fe->fontsize = 0;

    fe->timer_active = FALSE;

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

    return fe;
}

int main(int argc, char **argv)
{
    char *pname = argv[0];
    char *error;

    /*
     * Special standalone mode for generating puzzle IDs on the
     * command line. Useful for generating puzzles to be printed
     * out and solved offline (for puzzles where that even makes
     * sense - Solo, for example, is a lot more pencil-and-paper
     * friendly than Net!)
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
    if (argc > 1 && !strcmp(argv[1], "--generate")) {
	int n = 1;
	char *params = NULL, *seed = NULL;
	game_params *par;
	random_state *rs;
	char *parstr;

	if (argc > 2)
	    n = atoi(argv[2]);
	if (argc > 3)
	    params = argv[3];

        par = thegame.default_params();
	if (params) {
            if ( (seed = strchr(params, '#')) != NULL )
                *seed++ = '\0';
	    thegame.decode_params(par, params);
        } else {
        }
	parstr = thegame.encode_params(par, FALSE);

	{
	    void *seeddata;
	    int seedlen;
            if (seed) {
                seeddata = seed;
                seedlen = strlen(seed);
            } else {
                get_random_seed(&seeddata, &seedlen);
            }
	    rs = random_init(seeddata, seedlen);
	}

	while (n-- > 0) {
	    game_aux_info *aux = NULL;
	    char *desc = thegame.new_desc(par, rs, &aux);
	    printf("%s:%s\n", parstr, desc);
	    sfree(desc);
	    if (aux)
		thegame.free_aux_info(aux);
	}

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
