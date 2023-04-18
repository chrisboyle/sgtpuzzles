/*
 * nullfe.c: Null front-end code containing a bunch of boring stub
 * functions. Used to ensure successful linking when building the
 * various stand-alone solver binaries.
 */

#include <stdarg.h>

#include "puzzles.h"

void frontend_default_colour(frontend *fe, float *output) {}
void get_random_seed(void **randseed, int *randseedsize)
{ char *c = snewn(1, char); *c = 0; *randseed = c; *randseedsize = 1; }
void deactivate_timer(frontend *fe) {}
void activate_timer(frontend *fe) {}
struct drawing { char dummy; };
drawing *drawing_new(const drawing_api *api, midend *me, void *handle)
{ return snew(drawing); }
void drawing_free(drawing *dr) { sfree(dr); }
void draw_text(drawing *dr, int x, int y, int fonttype, int fontsize,
               int align, int colour, const char *text) {}
void draw_rect(drawing *dr, int x, int y, int w, int h, int colour) {}
void draw_line(drawing *dr, int x1, int y1, int x2, int y2, int colour) {}
void draw_thick_line(drawing *dr, float thickness,
		     float x1, float y1, float x2, float y2, int colour) {}
void draw_polygon(drawing *dr, const int *coords, int npoints,
                  int fillcolour, int outlinecolour) {}
void draw_circle(drawing *dr, int cx, int cy, int radius,
                 int fillcolour, int outlinecolour) {}
char *text_fallback(drawing *dr, const char *const *strings, int nstrings)
{ return dupstr(strings[0]); }
void clip(drawing *dr, int x, int y, int w, int h) {}
void unclip(drawing *dr) {}
void start_draw(drawing *dr) {}
void draw_update(drawing *dr, int x, int y, int w, int h) {}
void end_draw(drawing *dr) {}
struct blitter { char dummy; };
blitter *blitter_new(drawing *dr, int w, int h) { return snew(blitter); }
void blitter_free(drawing *dr, blitter *bl) { sfree(bl); }
void blitter_save(drawing *dr, blitter *bl, int x, int y) {}
void blitter_load(drawing *dr, blitter *bl, int x, int y) {}
int print_mono_colour(drawing *dr, int grey) { return 0; }
int print_grey_colour(drawing *dr, float grey) { return 0; }
int print_hatched_colour(drawing *dr, int hatch) { return 0; }
int print_rgb_mono_colour(drawing *dr, float r, float g, float b, int grey)
{ return 0; }
int print_rgb_grey_colour(drawing *dr, float r, float g, float b, float grey)
{ return 0; }
int print_rgb_hatched_colour(drawing *dr, float r, float g, float b, int hatch)
{ return 0; }
void print_line_width(drawing *dr, int width) {}
void print_line_dotted(drawing *dr, bool dotted) {}
void status_bar(drawing *dr, const char *text) {}
void document_add_puzzle(document *doc, const game *game, game_params *par,
			 game_state *st, game_state *st2) {}

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

#ifdef DEBUGGING
void debug_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}
#endif
