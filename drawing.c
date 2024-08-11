/*
 * drawing.c: Intermediary between the drawing interface as
 * presented to the back end, and that implemented by the front
 * end.
 * 
 * Mostly just looks up calls in a vtable and passes them through
 * unchanged. However, on the printing side it tracks print colours
 * so the front end API doesn't have to.
 * 
 * FIXME:
 * 
 *  - I'd _like_ to do automatic draw_updates, but it's a pain for
 *    draw_text in particular. I'd have to invent a front end API
 *    which retrieved the text bounds.
 *     + that might allow me to do the alignment centrally as well?
 * 	  * perhaps not, because PS can't return this information,
 * 	    so there would have to be a special case for it.
 *     + however, that at least doesn't stand in the way of using
 * 	 the text bounds for draw_update, because PS doesn't need
 * 	 draw_update since it's printing-only. Any _interactive_
 * 	 drawing API couldn't get away with refusing to tell you
 * 	 what parts of the screen a text draw had covered, because
 * 	 you would inevitably need to erase it later on.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif

#include "puzzles.h"

struct print_colour {
    int hatch;
    int hatch_when;		       /* 0=never 1=only-in-b&w 2=always */
    float r, g, b;
    float grey;
};

typedef struct drawing_internal {
    /* we implement data hiding by casting `struct drawing*` pointers
     * to `struct drawing_internal*` */
    struct drawing pub;

    /* private data */
    struct print_colour *colours;
    int ncolours, coloursize;
    float scale;
    /* `me' is only used in status_bar(), so print-oriented instances of
     * this may set it to NULL. */
    midend *me;
    char *laststatus;
} drawing_internal;

#define PRIVATE_CAST(dr) ((drawing_internal*)(dr))
#define PUBLIC_CAST(dri) ((drawing*)(dri))

/* See puzzles.h for a description of the version number. */
#define DRAWING_API_VERSION 1

drawing *drawing_new(const drawing_api *api, midend *me, void *handle)
{
    if(api->version != DRAWING_API_VERSION) {
	fatal("Drawing API version mismatch: expected: %d, actual: %d\n", DRAWING_API_VERSION, api->version);
	/* shouldn't get here */
	return NULL;
    }

    drawing_internal *dri = snew(drawing_internal);
    dri->pub.api = api;
    dri->pub.handle = handle;
    dri->colours = NULL;
    dri->ncolours = dri->coloursize = 0;
    dri->scale = 1.0F;
    dri->me = me;
    dri->laststatus = NULL;
    return PUBLIC_CAST(dri);
}

void drawing_free(drawing *dr)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    sfree(dri->laststatus);
    sfree(dri->colours);
    sfree(dri);
}

void draw_text(drawing *dr, int x, int y, int fonttype, int fontsize,
               int align, int colour, const char *text)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->draw_text(dr, x, y, fonttype, fontsize, align,
			    colour, text);
}

void draw_rect(drawing *dr, int x, int y, int w, int h, int colour)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->draw_rect(dr, x, y, w, h, colour);
}

void draw_line(drawing *dr, int x1, int y1, int x2, int y2, int colour)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->draw_line(dr, x1, y1, x2, y2, colour);
}

void draw_thick_line(drawing *dr, float thickness,
		     float x1, float y1, float x2, float y2, int colour)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    if (thickness < 1.0F)
        thickness = 1.0F;
    if (dri->pub.api->draw_thick_line) {
	dri->pub.api->draw_thick_line(dr, thickness,
				      x1, y1, x2, y2, colour);
    } else {
	/* We'll fake it up with a filled polygon.  The tweak to the
	 * thickness empirically compensates for rounding errors, because
	 * polygon rendering uses integer coordinates.
	 */
	float len = sqrt((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1));
	float tvhatx = (x2 - x1)/len * (thickness/2 - 0.2F);
	float tvhaty = (y2 - y1)/len * (thickness/2 - 0.2F);
	int p[8];

	p[0] = x1 - tvhaty;
	p[1] = y1 + tvhatx;
	p[2] = x2 - tvhaty;
	p[3] = y2 + tvhatx;
	p[4] = x2 + tvhaty;
	p[5] = y2 - tvhatx;
	p[6] = x1 + tvhaty;
	p[7] = y1 - tvhatx;
	dri->pub.api->draw_polygon(dr, p, 4, colour, colour);
    }
}

void draw_polygon(drawing *dr, const int *coords, int npoints,
                  int fillcolour, int outlinecolour)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->draw_polygon(dr, coords, npoints, fillcolour,
			       outlinecolour);
}

void draw_circle(drawing *dr, int cx, int cy, int radius,
                 int fillcolour, int outlinecolour)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->draw_circle(dr, cx, cy, radius, fillcolour,
			      outlinecolour);
}

void draw_update(drawing *dr, int x, int y, int w, int h)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    if (dri->pub.api->draw_update)
	dri->pub.api->draw_update(dr, x, y, w, h);
}

void clip(drawing *dr, int x, int y, int w, int h)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->clip(dr, x, y, w, h);
}

void unclip(drawing *dr)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->unclip(dr);
}

void start_draw(drawing *dr)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->start_draw(dr);
}

void end_draw(drawing *dr)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->end_draw(dr);
}

char *text_fallback(drawing *dr, const char *const *strings, int nstrings)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    int i;

    /*
     * If the drawing implementation provides one of these, use it.
     */
    if (dr && dri->pub.api->text_fallback)
	return dri->pub.api->text_fallback(dr, strings, nstrings);

    /*
     * Otherwise, do the simple thing and just pick the first string
     * that fits in plain ASCII. It will then need no translation
     * out of UTF-8.
     */
    for (i = 0; i < nstrings; i++) {
	const char *p;

	for (p = strings[i]; *p; p++)
	    if (*p & 0x80)
		break;
	if (!*p)
	    return dupstr(strings[i]);
    }

    /*
     * The caller was responsible for making sure _some_ string in
     * the list was in plain ASCII.
     */
    assert(!"Should never get here");
    return NULL;		       /* placate optimiser */
}

void status_bar(drawing *dr, const char *text)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    char *rewritten;

    if (!dri->pub.api->status_bar)
	return;

    assert(dri->me);

    rewritten = midend_rewrite_statusbar(dri->me, text);
    if (!dri->laststatus || strcmp(rewritten, dri->laststatus)) {
	dri->pub.api->status_bar(dr, rewritten);
	sfree(dri->laststatus);
	dri->laststatus = rewritten;
    } else {
	sfree(rewritten);
    }
}

blitter *blitter_new(drawing *dr, int w, int h)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    return dri->pub.api->blitter_new(dr, w, h);
}

void blitter_free(drawing *dr, blitter *bl)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->blitter_free(dr, bl);
}

void blitter_save(drawing *dr, blitter *bl, int x, int y)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->blitter_save(dr, bl, x, y);
}

void blitter_load(drawing *dr, blitter *bl, int x, int y)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->blitter_load(dr, bl, x, y);
}

void print_begin_doc(drawing *dr, int pages)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->begin_doc(dr, pages);
}

void print_begin_page(drawing *dr, int number)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->begin_page(dr, number);
}

void print_begin_puzzle(drawing *dr, float xm, float xc,
			float ym, float yc, int pw, int ph, float wmm,
			float scale)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->scale = scale;
    dri->ncolours = 0;
    dri->pub.api->begin_puzzle(dr, xm, xc, ym, yc, pw, ph, wmm);
}

void print_end_puzzle(drawing *dr)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->end_puzzle(dr);
    dri->scale = 1.0F;
}

void print_end_page(drawing *dr, int number)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->end_page(dr, number);
}

void print_end_doc(drawing *dr)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->end_doc(dr);
}

void print_get_colour(drawing *dr, int colour, bool printing_in_colour,
		      int *hatch, float *r, float *g, float *b)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    assert(colour >= 0 && colour < dri->ncolours);
    if (dri->colours[colour].hatch_when == 2 ||
	(dri->colours[colour].hatch_when == 1 && !printing_in_colour)) {
	*hatch = dri->colours[colour].hatch;
    } else {
	*hatch = -1;
	if (printing_in_colour) {
	    *r = dri->colours[colour].r;
	    *g = dri->colours[colour].g;
	    *b = dri->colours[colour].b;
	} else {
	    *r = *g = *b = dri->colours[colour].grey;
	}
    }
}

static int print_generic_colour(drawing *dr, float r, float g, float b,
				float grey, int hatch, int hatch_when)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    if (dri->ncolours >= dri->coloursize) {
	dri->coloursize = dri->ncolours + 16;
	dri->colours = sresize(dri->colours, dri->coloursize,
			      struct print_colour);
    }
    dri->colours[dri->ncolours].hatch = hatch;
    dri->colours[dri->ncolours].hatch_when = hatch_when;
    dri->colours[dri->ncolours].r = r;
    dri->colours[dri->ncolours].g = g;
    dri->colours[dri->ncolours].b = b;
    dri->colours[dri->ncolours].grey = grey;
    return dri->ncolours++;
}

int print_mono_colour(drawing *dr, int grey)
{
    return print_generic_colour(dr, grey, grey, grey, grey, -1, 0);
}

int print_grey_colour(drawing *dr, float grey)
{
    return print_generic_colour(dr, grey, grey, grey, grey, -1, 0);
}

int print_hatched_colour(drawing *dr, int hatch)
{
    return print_generic_colour(dr, 0, 0, 0, 0, hatch, 2);
}

int print_rgb_mono_colour(drawing *dr, float r, float g, float b, int grey)
{
    return print_generic_colour(dr, r, g, b, grey, -1, 0);
}

int print_rgb_grey_colour(drawing *dr, float r, float g, float b, float grey)
{
    return print_generic_colour(dr, r, g, b, grey, -1, 0);
}

int print_rgb_hatched_colour(drawing *dr, float r, float g, float b, int hatch)
{
    return print_generic_colour(dr, r, g, b, 0, hatch, 1);
}

void print_line_width(drawing *dr, int width)
{
    drawing_internal *dri = PRIVATE_CAST(dr);

    /*
     * I don't think it's entirely sensible to have line widths be
     * entirely relative to the puzzle size; there is a point
     * beyond which lines are just _stupidly_ thick. On the other
     * hand, absolute line widths aren't particularly nice either
     * because they start to feel a bit feeble at really large
     * scales.
     * 
     * My experimental answer is to scale line widths as the
     * _square root_ of the main puzzle scale. Double the puzzle
     * size, and the line width multiplies by 1.4.
     */
    dri->pub.api->line_width(dr, (float)sqrt(dri->scale) * width);
}

void print_line_dotted(drawing *dr, bool dotted)
{
    drawing_internal *dri = PRIVATE_CAST(dr);
    dri->pub.api->line_dotted(dr, dotted);
}
