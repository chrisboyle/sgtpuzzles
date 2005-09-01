/*
 * nullfe.c: Null front-end code containing a bunch of boring stub
 * functions. Used to ensure successful linking when building the
 * various stand-alone solver binaries.
 */

#include <stdarg.h>

#include "puzzles.h"

void frontend_default_colour(frontend *fe, float *output) {}
void draw_text(drawing *dr, int x, int y, int fonttype, int fontsize,
               int align, int colour, char *text) {}
void draw_rect(drawing *dr, int x, int y, int w, int h, int colour) {}
void draw_line(drawing *dr, int x1, int y1, int x2, int y2, int colour) {}
void draw_polygon(drawing *dr, int *coords, int npoints,
                  int fillcolour, int outlinecolour) {}
void draw_circle(drawing *dr, int cx, int cy, int radius,
                 int fillcolour, int outlinecolour) {}
void clip(drawing *dr, int x, int y, int w, int h) {}
void unclip(drawing *dr) {}
void start_draw(drawing *dr) {}
void draw_update(drawing *dr, int x, int y, int w, int h) {}
void end_draw(drawing *dr) {}
blitter *blitter_new(drawing *dr, int w, int h) {return NULL;}
void blitter_free(drawing *dr, blitter *bl) {}
void blitter_save(drawing *dr, blitter *bl, int x, int y) {}
void blitter_load(drawing *dr, blitter *bl, int x, int y) {}
int print_mono_colour(drawing *dr, int grey) { return 0; }
int print_rgb_colour(drawing *dr, int hatch, float r, float g, float b)
{ return 0; }
void print_line_width(drawing *dr, int width) {}
void midend_supersede_game_desc(midend *me, char *desc, char *privdesc) {}
void status_bar(drawing *dr, char *text) {}

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

