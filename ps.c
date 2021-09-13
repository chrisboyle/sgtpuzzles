/*
 * ps.c: PostScript printing functions.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "puzzles.h"

struct psdata {
    FILE *fp;
    bool colour;
    int ytop;
    bool clipped;
    float hatchthick, hatchspace;
    int gamewidth, gameheight;
    drawing *drawing;
};

static void ps_printf(psdata *ps, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(ps->fp, fmt, ap);
    va_end(ap);
}

static void ps_fill(psdata *ps, int colour)
{
    int hatch;
    float r, g, b;

    print_get_colour(ps->drawing, colour, ps->colour, &hatch, &r, &g, &b);

    if (hatch < 0) {
	if (ps->colour)
	    ps_printf(ps, "%g %g %g setrgbcolor fill\n", r, g, b);
	else
	    ps_printf(ps, "%g setgray fill\n", r);
    } else {
	/* Clip to the region. */
	ps_printf(ps, "gsave clip\n");
	/* Hatch the entire game printing area. */
	ps_printf(ps, "newpath\n");
	if (hatch == HATCH_VERT || hatch == HATCH_PLUS)
	    ps_printf(ps, "0 %g %d {\n"
		      "  0 moveto 0 %d rlineto\n"
		      "} for\n", ps->hatchspace, ps->gamewidth,
		      ps->gameheight);
	if (hatch == HATCH_HORIZ || hatch == HATCH_PLUS)
	    ps_printf(ps, "0 %g %d {\n"
		      "  0 exch moveto %d 0 rlineto\n"
		      "} for\n", ps->hatchspace, ps->gameheight,
		      ps->gamewidth);
	if (hatch == HATCH_SLASH || hatch == HATCH_X)
	    ps_printf(ps, "%d %g %d {\n"
		      "  0 moveto %d dup rlineto\n"
		      "} for\n", -ps->gameheight, ps->hatchspace * ROOT2,
		      ps->gamewidth, max(ps->gamewidth, ps->gameheight));
	if (hatch == HATCH_BACKSLASH || hatch == HATCH_X)
	    ps_printf(ps, "0 %g %d {\n"
		      "  0 moveto %d neg dup neg rlineto\n"
		      "} for\n", ps->hatchspace * ROOT2,
		      ps->gamewidth+ps->gameheight,
		      max(ps->gamewidth, ps->gameheight));
	ps_printf(ps, "0 setgray %g setlinewidth stroke grestore\n",
		  ps->hatchthick);
    }
}

static void ps_setcolour_internal(psdata *ps, int colour, const char *suffix)
{
    int hatch;
    float r, g, b;

    print_get_colour(ps->drawing, colour, ps->colour, &hatch, &r, &g, &b);

    /*
     * Stroking in hatched colours is not permitted.
     */
    assert(hatch < 0);
    
    if (ps->colour)
	ps_printf(ps, "%g %g %g setrgbcolor%s\n", r, g, b, suffix);
    else
	ps_printf(ps, "%g setgray%s\n", r, suffix);
}

static void ps_setcolour(psdata *ps, int colour)
{
    ps_setcolour_internal(ps, colour, "");
}

static void ps_stroke(psdata *ps, int colour)
{
    ps_setcolour_internal(ps, colour, " stroke");
}

static void ps_draw_text(void *handle, int x, int y, int fonttype,
			 int fontsize, int align, int colour,
                         const char *text)
{
    psdata *ps = (psdata *)handle;

    y = ps->ytop - y;
    ps_setcolour(ps, colour);
    ps_printf(ps, "/%s findfont %d scalefont setfont\n",
	      fonttype == FONT_FIXED ? "Courier-L1" : "Helvetica-L1",
	      fontsize);
    if (align & ALIGN_VCENTRE) {
	ps_printf(ps, "newpath 0 0 moveto (X) true charpath flattenpath"
		  " pathbbox\n"
		  "3 -1 roll add 2 div %d exch sub %d exch moveto pop pop\n",
		  y, x);
    } else {
	ps_printf(ps, "%d %d moveto\n", x, y);
    }
    ps_printf(ps, "(");
    while (*text) {
	if (*text == '\\' || *text == '(' || *text == ')')
	    ps_printf(ps, "\\");
	ps_printf(ps, "%c", *text);
	text++;
    }
    ps_printf(ps, ") ");
    if (align & (ALIGN_HCENTRE | ALIGN_HRIGHT))
	ps_printf(ps, "dup stringwidth pop %sneg 0 rmoveto show\n",
		  (align & ALIGN_HCENTRE) ? "2 div " : "");
    else
	ps_printf(ps, "show\n");
}

static void ps_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
    psdata *ps = (psdata *)handle;

    y = ps->ytop - y;
    /*
     * Offset by half a pixel for the exactness requirement.
     */
    ps_printf(ps, "newpath %g %g moveto %d 0 rlineto 0 %d rlineto"
	      " %d 0 rlineto closepath\n", x - 0.5, y + 0.5, w, -h, -w);
    ps_fill(ps, colour);
}

static void ps_draw_line(void *handle, int x1, int y1, int x2, int y2,
			 int colour)
{
    psdata *ps = (psdata *)handle;

    y1 = ps->ytop - y1;
    y2 = ps->ytop - y2;
    ps_printf(ps, "newpath %d %d moveto %d %d lineto\n", x1, y1, x2, y2);
    ps_stroke(ps, colour);
}

static void ps_draw_polygon(void *handle, const int *coords, int npoints,
			    int fillcolour, int outlinecolour)
{
    psdata *ps = (psdata *)handle;

    int i;

    ps_printf(ps, "newpath %d %d moveto\n", coords[0], ps->ytop - coords[1]);

    for (i = 1; i < npoints; i++)
	ps_printf(ps, "%d %d lineto\n", coords[i*2], ps->ytop - coords[i*2+1]);

    ps_printf(ps, "closepath\n");

    if (fillcolour >= 0) {
	ps_printf(ps, "gsave\n");
	ps_fill(ps, fillcolour);
	ps_printf(ps, "grestore\n");
    }
    ps_stroke(ps, outlinecolour);
}

static void ps_draw_circle(void *handle, int cx, int cy, int radius,
			   int fillcolour, int outlinecolour)
{
    psdata *ps = (psdata *)handle;

    cy = ps->ytop - cy;

    ps_printf(ps, "newpath %d %d %d 0 360 arc closepath\n", cx, cy, radius);

    if (fillcolour >= 0) {
	ps_printf(ps, "gsave\n");
	ps_fill(ps, fillcolour);
	ps_printf(ps, "grestore\n");
    }
    ps_stroke(ps, outlinecolour);
}

static void ps_unclip(void *handle)
{
    psdata *ps = (psdata *)handle;

    assert(ps->clipped);
    ps_printf(ps, "grestore\n");
    ps->clipped = false;
}
 
static void ps_clip(void *handle, int x, int y, int w, int h)
{
    psdata *ps = (psdata *)handle;

    if (ps->clipped)
	ps_unclip(ps);

    y = ps->ytop - y;
    /*
     * Offset by half a pixel for the exactness requirement.
     */
    ps_printf(ps, "gsave\n");
    ps_printf(ps, "newpath %g %g moveto %d 0 rlineto 0 %d rlineto"
	      " %d 0 rlineto closepath\n", x - 0.5, y + 0.5, w, -h, -w);
    ps_printf(ps, "clip\n");
    ps->clipped = true;
}

static void ps_line_width(void *handle, float width)
{
    psdata *ps = (psdata *)handle;

    ps_printf(ps, "%g setlinewidth\n", width);
}

static void ps_line_dotted(void *handle, bool dotted)
{
    psdata *ps = (psdata *)handle;

    if (dotted) {
	ps_printf(ps, "[ currentlinewidth 3 mul ] 0 setdash\n");
    } else {
	ps_printf(ps, "[ ] 0 setdash\n");
    }
}

static char *ps_text_fallback(void *handle, const char *const *strings,
			      int nstrings)
{
    /*
     * We can handle anything in ISO 8859-1, and we'll manually
     * translate it out of UTF-8 for the purpose.
     */
    int i, maxlen;
    char *ret;

    maxlen = 0;
    for (i = 0; i < nstrings; i++) {
	int len = strlen(strings[i]);
	if (maxlen < len) maxlen = len;
    }

    ret = snewn(maxlen + 1, char);

    for (i = 0; i < nstrings; i++) {
	const char *p = strings[i];
	char *q = ret;

	while (*p) {
	    int c = (unsigned char)*p++;
	    if (c < 0x80) {
		*q++ = c;	       /* ASCII */
	    } else if ((c == 0xC2 || c == 0xC3) && (*p & 0xC0) == 0x80) {
		*q++ = (c << 6) | (*p++ & 0x3F);   /* top half of 8859-1 */
	    } else {
		break;
	    }
	}

	if (!*p) {
	    *q = '\0';
	    return ret;
	}
    }

    assert(!"Should never reach here");
    return NULL;
}

static void ps_begin_doc(void *handle, int pages)
{
    psdata *ps = (psdata *)handle;

    fputs("%!PS-Adobe-3.0\n", ps->fp);
    fputs("%%Creator: Simon Tatham's Portable Puzzle Collection\n", ps->fp);
    fputs("%%DocumentData: Clean7Bit\n", ps->fp);
    fputs("%%LanguageLevel: 1\n", ps->fp);
    fprintf(ps->fp, "%%%%Pages: %d\n", pages);
    fputs("%%DocumentNeededResources:\n", ps->fp);
    fputs("%%+ font Helvetica\n", ps->fp);
    fputs("%%+ font Courier\n", ps->fp);
    fputs("%%EndComments\n", ps->fp);
    fputs("%%BeginSetup\n", ps->fp);
    fputs("%%IncludeResource: font Helvetica\n", ps->fp);
    fputs("%%IncludeResource: font Courier\n", ps->fp);
    fputs("%%EndSetup\n", ps->fp);
    fputs("%%BeginProlog\n", ps->fp);
    /*
     * Re-encode Helvetica and Courier into ISO-8859-1, which gives
     * us times and divide signs - and also (according to the
     * Language Reference Manual) a bonus in that the ASCII '-' code
     * point now points to a minus sign instead of a hyphen.
     */
    fputs("/Helvetica findfont " /* get the font dictionary */
	  "dup maxlength dict dup begin " /* create and open a new dict */
	  "exch " /* move the original font to top of stack */
	  "{1 index /FID ne {def} {pop pop} ifelse} forall "
				       /* copy everything except FID */
	  "/Encoding ISOLatin1Encoding def "
			      /* set the thing we actually wanted to change */
	  "/FontName /Helvetica-L1 def " /* set a new font name */
	  "FontName end exch definefont" /* and define the font */
	  "\n", ps->fp);
    fputs("/Courier findfont " /* get the font dictionary */
	  "dup maxlength dict dup begin " /* create and open a new dict */
	  "exch " /* move the original font to top of stack */
	  "{1 index /FID ne {def} {pop pop} ifelse} forall "
				       /* copy everything except FID */
	  "/Encoding ISOLatin1Encoding def "
			      /* set the thing we actually wanted to change */
	  "/FontName /Courier-L1 def " /* set a new font name */
	  "FontName end exch definefont" /* and define the font */
	  "\n", ps->fp);
    fputs("%%EndProlog\n", ps->fp);
}

static void ps_begin_page(void *handle, int number)
{
    psdata *ps = (psdata *)handle;

    fprintf(ps->fp, "%%%%Page: %d %d\ngsave save\n%g dup scale\n",
	    number, number, 72.0 / 25.4);
}

static void ps_begin_puzzle(void *handle, float xm, float xc,
			    float ym, float yc, int pw, int ph, float wmm)
{
    psdata *ps = (psdata *)handle;

    fprintf(ps->fp, "gsave\n"
	    "clippath flattenpath pathbbox pop pop translate\n"
	    "clippath flattenpath pathbbox 4 2 roll pop pop\n"
	    "exch %g mul %g add exch dup %g mul %g add sub translate\n"
	    "%g dup scale\n"
	    "0 -%d translate\n", xm, xc, ym, yc, wmm/pw, ph);
    ps->ytop = ph;
    ps->clipped = false;
    ps->gamewidth = pw;
    ps->gameheight = ph;
    ps->hatchthick = 0.2 * pw / wmm;
    ps->hatchspace = 1.0 * pw / wmm;
}

static void ps_end_puzzle(void *handle)
{
    psdata *ps = (psdata *)handle;

    fputs("grestore\n", ps->fp);
}

static void ps_end_page(void *handle, int number)
{
    psdata *ps = (psdata *)handle;

    fputs("restore grestore showpage\n", ps->fp);
}

static void ps_end_doc(void *handle)
{
    psdata *ps = (psdata *)handle;

    fputs("%%EOF\n", ps->fp);
}

static const struct drawing_api ps_drawing = {
    ps_draw_text,
    ps_draw_rect,
    ps_draw_line,
    ps_draw_polygon,
    ps_draw_circle,
    NULL /* draw_update */,
    ps_clip,
    ps_unclip,
    NULL /* start_draw */,
    NULL /* end_draw */,
    NULL /* status_bar */,
    NULL /* blitter_new */,
    NULL /* blitter_free */,
    NULL /* blitter_save */,
    NULL /* blitter_load */,
    ps_begin_doc,
    ps_begin_page,
    ps_begin_puzzle,
    ps_end_puzzle,
    ps_end_page,
    ps_end_doc,
    ps_line_width,
    ps_line_dotted,
    ps_text_fallback,
};

psdata *ps_init(FILE *outfile, bool colour)
{
    psdata *ps = snew(psdata);

    ps->fp = outfile;
    ps->colour = colour;
    ps->ytop = 0;
    ps->clipped = false;
    ps->hatchthick = ps->hatchspace = ps->gamewidth = ps->gameheight = 0;
    ps->drawing = drawing_new(&ps_drawing, NULL, ps);

    return ps;
}

void ps_free(psdata *ps)
{
    drawing_free(ps->drawing);
    sfree(ps);
}

drawing *ps_drawing_api(psdata *ps)
{
    return ps->drawing;
}
