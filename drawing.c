/*
 * drawing.c: Intermediary between the drawing interface as
 * presented to the back end, and that implemented by the front
 * end.
 * 
 * Mostly just looks up calls in a vtable and passes them through
 * unchanged. However, on the printing side it tracks print colours
 * so the front end API doesn't have to.
 * 
 * FIXME: could we also sort out rewrite_statusbar in here? Also
 * I'd _like_ to do automatic draw_updates, but it's a pain for
 * draw_text in particular - I could invent a front end API which
 * retrieved the text bounds and then do the alignment myself as
 * well, except that that doesn't work for PS. As usual.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "puzzles.h"

struct print_colour {
    int hatch;
    float r, g, b;
};

struct drawing {
    const drawing_api *api;
    void *handle;
    struct print_colour *colours;
    int ncolours, coloursize;
    float scale;
};

drawing *drawing_init(const drawing_api *api, void *handle)
{
    drawing *dr = snew(drawing);
    dr->api = api;
    dr->handle = handle;
    dr->colours = NULL;
    dr->ncolours = dr->coloursize = 0;
    dr->scale = 1.0F;
    return dr;
}

void drawing_free(drawing *dr)
{
    sfree(dr->colours);
    sfree(dr);
}

void draw_text(drawing *dr, int x, int y, int fonttype, int fontsize,
               int align, int colour, char *text)
{
    dr->api->draw_text(dr->handle, x, y, fonttype, fontsize, align,
		       colour, text);
}

void draw_rect(drawing *dr, int x, int y, int w, int h, int colour)
{
    dr->api->draw_rect(dr->handle, x, y, w, h, colour);
}

void draw_line(drawing *dr, int x1, int y1, int x2, int y2, int colour)
{
    dr->api->draw_line(dr->handle, x1, y1, x2, y2, colour);
}

void draw_polygon(drawing *dr, int *coords, int npoints,
                  int fillcolour, int outlinecolour)
{
    dr->api->draw_polygon(dr->handle, coords, npoints, fillcolour,
			  outlinecolour);
}

void draw_circle(drawing *dr, int cx, int cy, int radius,
                 int fillcolour, int outlinecolour)
{
    dr->api->draw_circle(dr->handle, cx, cy, radius, fillcolour,
			 outlinecolour);
}

void draw_update(drawing *dr, int x, int y, int w, int h)
{
    if (dr->api->draw_update)
	dr->api->draw_update(dr->handle, x, y, w, h);
}

void clip(drawing *dr, int x, int y, int w, int h)
{
    dr->api->clip(dr->handle, x, y, w, h);
}

void unclip(drawing *dr)
{
    dr->api->unclip(dr->handle);
}

void start_draw(drawing *dr)
{
    dr->api->start_draw(dr->handle);
}

void end_draw(drawing *dr)
{
    dr->api->end_draw(dr->handle);
}

void status_bar(drawing *dr, char *text)
{
    if (dr->api->status_bar)
	dr->api->status_bar(dr->handle, text);
}

blitter *blitter_new(drawing *dr, int w, int h)
{
    return dr->api->blitter_new(dr->handle, w, h);
}

void blitter_free(drawing *dr, blitter *bl)
{
    dr->api->blitter_free(dr->handle, bl);
}

void blitter_save(drawing *dr, blitter *bl, int x, int y)
{
    dr->api->blitter_save(dr->handle, bl, x, y);
}

void blitter_load(drawing *dr, blitter *bl, int x, int y)
{
    dr->api->blitter_load(dr->handle, bl, x, y);
}

void print_begin_doc(drawing *dr, int pages)
{
    dr->api->begin_doc(dr->handle, pages);
}

void print_begin_page(drawing *dr, int number)
{
    dr->api->begin_page(dr->handle, number);
}

void print_begin_puzzle(drawing *dr, float xm, float xc,
			float ym, float yc, int pw, int ph, float wmm,
			float scale)
{
    dr->scale = scale;
    dr->ncolours = 0;
    dr->api->begin_puzzle(dr->handle, xm, xc, ym, yc, pw, ph, wmm);
}

void print_end_puzzle(drawing *dr)
{
    dr->api->end_puzzle(dr->handle);
    dr->scale = 1.0F;
}

void print_end_page(drawing *dr, int number)
{
    dr->api->end_page(dr->handle, number);
}

void print_end_doc(drawing *dr)
{
    dr->api->end_doc(dr->handle);
}

void print_get_colour(drawing *dr, int colour, int *hatch,
		      float *r, float *g, float *b)
{
    assert(colour >= 0 && colour < dr->ncolours);
    *hatch = dr->colours[colour].hatch;
    *r = dr->colours[colour].r;
    *g = dr->colours[colour].g;
    *b = dr->colours[colour].b;
}

int print_rgb_colour(drawing *dr, int hatch, float r, float g, float b)
{
    if (dr->ncolours >= dr->coloursize) {
	dr->coloursize = dr->ncolours + 16;
	dr->colours = sresize(dr->colours, dr->coloursize,
			      struct print_colour);
    }
    dr->colours[dr->ncolours].hatch = hatch;
    dr->colours[dr->ncolours].r = r;
    dr->colours[dr->ncolours].g = g;
    dr->colours[dr->ncolours].b = b;
    return dr->ncolours++;
}

int print_grey_colour(drawing *dr, int hatch, float grey)
{
    return print_rgb_colour(dr, hatch, grey, grey, grey);
}

int print_mono_colour(drawing *dr, int grey)
{
    return print_rgb_colour(dr, grey ? HATCH_CLEAR : HATCH_SOLID,
			    grey, grey, grey);
}

void print_line_width(drawing *dr, int width)
{
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
    dr->api->line_width(dr->handle, sqrt(dr->scale) * width);
}
