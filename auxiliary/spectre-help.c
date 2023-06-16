/*
 * Common code between spectre-test and spectre-gen, since both of
 * them want to output SVG graphics.
 */

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "puzzles.h"
#include "tree234.h"
#include "spectre-internal.h"
#include "spectre-tables-extra.h"
#include "spectre-help.h"

struct HexData {
    const int *edges;
};

static const struct HexData hexdata[] = {
    #define HEXDATA_ENTRY(x) { edges_##x },
    HEX_LETTERS(HEXDATA_ENTRY)
    #undef HEXDATA_ENTRY
};

const char *hex_names[10] = {
    "G", "D", "J", "L", "X", "P", "S", "F", "Y",
    "" /* NO_HEX */
};

Graphics *gr_new(const char *filename, double xmin, double xmax,
                 double ymin, double ymax, double scale)
{
    Graphics *gr = snew(Graphics);
    if (!strcmp(filename, "-")) {
        gr->fp = stdout;
        gr->close_file = false;
    } else {
        gr->fp = fopen(filename, "w");
        if (!gr->fp) {
            fprintf(stderr, "%s: open: %s\n", filename, strerror(errno));
            exit(1);
        }
        gr->close_file = true;
    }

    fprintf(gr->fp, "<?xml version=\"1.0\" encoding=\"UTF-8\" "
            "standalone=\"no\"?>\n");
    fprintf(gr->fp, "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "version=\"1.1\" width=\"%f\" height=\"%f\">\n",
            (xmax - xmin) * scale, (ymax - ymin) * scale);

    gr->absscale = fabs(scale);
    gr->xoff = -xmin * scale;
    gr->xscale = scale;
    /* invert y axis for SVG top-down coordinate system */
    gr->yoff = ymax * scale;
    gr->yscale = -scale;

    /* Defaults, which can be overridden by the caller immediately
     * after this constructor returns */
    gr->jigsaw_mode = false;
    gr->vertex_blobs = true;
    gr->number_cells = true;
    gr->four_colour = false;
    gr->arcs = false;
    gr->linewidth = 1.5;

    gr->started = false;

    return gr;
}

void gr_free(Graphics *gr)
{
    if (!gr)
        return;
    fprintf(gr->fp, "</svg>\n");
    if (gr->close_file)
        fclose(gr->fp);
    sfree(gr);
}

static void gr_ensure_started(Graphics *gr)
{
    if (gr->started)
        return;

    fprintf(gr->fp, "<style type=\"text/css\">\n");
    fprintf(gr->fp, "path { fill: none; stroke: black; stroke-width: %f; "
            "stroke-linejoin: round; stroke-linecap: round; }\n",
            gr->linewidth);
    fprintf(gr->fp, "text { fill: black; font-family: Sans; "
            "text-anchor: middle; text-align: center; }\n");
    if (gr->four_colour) {
        fprintf(gr->fp, ".c0 { fill: rgb(255, 178, 178); }\n");
        fprintf(gr->fp, ".c1 { fill: rgb(255, 255, 178); }\n");
        fprintf(gr->fp, ".c2 { fill: rgb(178, 255, 178); }\n");
        fprintf(gr->fp, ".c3 { fill: rgb(153, 153, 255); }\n");
    } else {
        fprintf(gr->fp, ".G { fill: rgb(255, 128, 128); }\n");
        fprintf(gr->fp, ".G1 { fill: rgb(255, 64, 64); }\n");
        fprintf(gr->fp, ".F { fill: rgb(255, 192, 128); }\n");
        fprintf(gr->fp, ".Y { fill: rgb(255, 255, 128); }\n");
        fprintf(gr->fp, ".S { fill: rgb(128, 255, 128); }\n");
        fprintf(gr->fp, ".D { fill: rgb(128, 255, 255); }\n");
        fprintf(gr->fp, ".P { fill: rgb(128, 128, 255); }\n");
        fprintf(gr->fp, ".X { fill: rgb(192, 128, 255); }\n");
        fprintf(gr->fp, ".J { fill: rgb(255, 128, 255); }\n");
        fprintf(gr->fp, ".L { fill: rgb(128, 128, 128); }\n");
        fprintf(gr->fp, ".optional { stroke-dasharray: 5; }\n");
        fprintf(gr->fp, ".arrow { fill: rgba(0, 0, 0, 0.2); "
                "stroke: none; }\n");
    }
    fprintf(gr->fp, "</style>\n");

    gr->started = true;
}

/* Logical coordinates in our mathematical space */
GrCoords gr_logcoords(Point p)
{
    double rt3o2 = sqrt(3) / 2;
    GrCoords r = {
        p.coeffs[0] + rt3o2 * p.coeffs[1] + 0.5 * p.coeffs[2],
        p.coeffs[3] + rt3o2 * p.coeffs[2] + 0.5 * p.coeffs[1],
    };
    return r;
}

/* Physical coordinates in the output image */
GrCoords gr_log2phys(Graphics *gr, GrCoords c)
{
    c.x = gr->xoff + gr->xscale * c.x;
    c.y = gr->yoff + gr->yscale * c.y;
    return c;
}
GrCoords gr_physcoords(Graphics *gr, Point p)
{
    return gr_log2phys(gr, gr_logcoords(p));
}

void gr_draw_text(Graphics *gr, GrCoords logpos, double logheight,
                  const char *text)
{
    GrCoords pos;
    double height;

    if (!gr)
        return;
    gr_ensure_started(gr);

    pos = gr_log2phys(gr, logpos);
    height = gr->absscale * logheight;
    fprintf(gr->fp, "<text style=\"font-size: %fpx\" x=\"%f\" y=\"%f\">"
            "%s</text>\n", height, pos.x, pos.y + 0.35 * height, text);
}

void gr_draw_path(Graphics *gr, const char *classes, const GrCoords *phys,
                  size_t n, bool closed)
{
    size_t i;

    if (!gr)
        return;
    gr_ensure_started(gr);

    fprintf(gr->fp, "<path class=\"%s\" d=\"", classes);
    for (i = 0; i < n; i++) {
        GrCoords c = phys[i];
        if (i == 0)
            fprintf(gr->fp, "M %f %f", c.x, c.y);
        else if (gr->arcs)
            fprintf(gr->fp, "A %f %f 10 0 %zu %f %f",
                    gr->absscale, gr->absscale, i&1, c.x, c.y);
        else
            fprintf(gr->fp, "L %f %f", c.x, c.y);
    }
    if (gr->arcs) {
        /* Explicitly return to the starting point so as to curve the
         * final edge */
        fprintf(gr->fp, "A %f %f 10 0 0 %f %f",
                gr->absscale, gr->absscale, phys[0].x, phys[0].y);
    }
    if (closed)
        fprintf(gr->fp, " z");
    fprintf(gr->fp, "\"/>\n");
}

void gr_draw_blob(Graphics *gr, const char *classes, GrCoords log,
                  double logradius)
{
    GrCoords centre;

    if (!gr)
        return;
    gr_ensure_started(gr);

    centre = gr_log2phys(gr, log);
    fprintf(gr->fp, "<circle class=\"%s\" cx=\"%f\" cy=\"%f\" r=\"%f\"/>\n",
            classes, centre.x, centre.y, gr->absscale * logradius);
}

void gr_draw_hex(Graphics *gr, unsigned index, Hex htype,
                 const Point *vertices)
{
    size_t i;
    Point centre;

    if (!gr)
        return;
    gr_ensure_started(gr);

    /* Draw the actual hexagon, in its own colour */
    if (!gr->jigsaw_mode) {
        GrCoords phys[6];
        for (i = 0; i < 6; i++)
            phys[i] = gr_physcoords(gr, vertices[i]);
        gr_draw_path(gr, (index == 7 && htype == NO_HEX ?
                          "optional" : hex_names[htype]), phys, 6, true);
    } else {
        GrCoords phys[66];
        size_t pos = 0;
        const struct HexData *hd = &hexdata[htype];

        for (i = 0; i < 6; i++) {
            int edge_type = hd->edges[i];
            int sign = edge_type < 0 ? -1 : +1;
            int edge_abs = abs(edge_type);
            int left_sign = (edge_abs & 4) ? sign : edge_type == 0 ? +1 : 0;
            int mid_sign = (edge_abs & 2) ? sign : 0;
            int right_sign = (edge_abs & 1) ? sign : edge_type == 0 ? -1 : 0;

            GrCoords start = gr_physcoords(gr, vertices[i]);
            GrCoords end = gr_physcoords(gr, vertices[(i+1) % 6]);
            GrCoords x = { (end.x - start.x) / 7, (end.y - start.y) / 7 };
            GrCoords y = { -x.y, +x.x };

#define addpoint(X, Y) do {                             \
                GrCoords p = {                          \
                    start.x + (X) * x.x + (Y) * y.x,    \
                    start.y + (X) * x.y + (Y) * y.y,    \
                };                                      \
                phys[pos++] = p;                        \
            } while (0)

            if (sign < 0) {
                int tmp = right_sign;
                right_sign = left_sign;
                left_sign = tmp;
            }

            addpoint(0, 0);
            if (left_sign) {
                addpoint(1, 0);
                addpoint(2, left_sign);
                addpoint(2, 0);
            }
            if (mid_sign) {
                addpoint(3, 0);
                addpoint(3, mid_sign);
                addpoint(4, mid_sign);
                addpoint(4, 0);
            }
            if (right_sign) {
                addpoint(5, 0);
                addpoint(5, right_sign);
                addpoint(6, 0);
            }

#undef addpoint

        }
        gr_draw_path(gr, hex_names[htype], phys, pos, true);
    }

    /* Find the centre of the hex */
    for (i = 0; i < 4; i++)
        centre.coeffs[i] = 0;
    for (i = 0; i < 6; i++)
        centre = point_add(centre, vertices[i]);
    for (i = 0; i < 4; i++)
        centre.coeffs[i] /= 6;

    /* Draw an arrow towards vertex 0 of the hex */
    if (gr->hex_arrows) {
        double ext = 0.6;
        double headlen = 0.3, thick = 0.08, headwid = 0.25;
        GrCoords top = gr_physcoords(gr, vertices[0]);
        GrCoords bot = gr_physcoords(gr, vertices[3]);
        GrCoords mid = gr_physcoords(gr, centre);
        GrCoords base = { mid.x + ext * (bot.x - mid.x),
                          mid.y + ext * (bot.y - mid.y) };
        GrCoords tip = { mid.x + ext * (top.x - mid.x),
                         mid.y + ext * (top.y - mid.y) };
        GrCoords len = { tip.x - base.x, tip.y - base.y };
        GrCoords perp = { -len.y, +len.x };
        GrCoords basep = { base.x+perp.x*thick, base.y+perp.y*thick };
        GrCoords basen = { base.x-perp.x*thick, base.y-perp.y*thick };
        GrCoords hbase = { tip.x-len.x*headlen, tip.y-len.y*headlen };
        GrCoords headp = { hbase.x+perp.x*thick, hbase.y+perp.y*thick };
        GrCoords headn = { hbase.x-perp.x*thick, hbase.y-perp.y*thick };
        GrCoords headP = { hbase.x+perp.x*headwid, hbase.y+perp.y*headwid };
        GrCoords headN = { hbase.x-perp.x*headwid, hbase.y-perp.y*headwid };

        GrCoords phys[] = {
            basep, headp, headP, tip, headN, headn, basen
        };

        gr_draw_path(gr, "arrow", phys, lenof(phys), true);
    }

    /*
     * Label the hex with its index and type.
     */
    if (gr->number_cells) {
        char buf[64];
        if (index == (unsigned)-1) {
            if (htype == NO_HEX)
                buf[0] = '\0';
            else
                strcpy(buf, hex_names[htype]);
        } else {
            if (htype == NO_HEX)
                sprintf(buf, "%u", index);
            else
                sprintf(buf, "%u (%s)", index, hex_names[htype]);
        }
        if (buf[0])
            gr_draw_text(gr, gr_logcoords(centre), 1.2, buf);
    }
}

void gr_draw_spectre(Graphics *gr, Hex container, unsigned index,
                     const Point *vertices)
{
    size_t i;
    GrCoords log[14];
    GrCoords centre;

    if (!gr)
        return;
    gr_ensure_started(gr);

    for (i = 0; i < 14; i++)
        log[i] = gr_logcoords(vertices[i]);

    /* Draw the actual Spectre */
    {
        GrCoords phys[14];
        char class[16];
        for (i = 0; i < 14; i++)
            phys[i] = gr_log2phys(gr, log[i]);
        if (gr->four_colour) {
            sprintf(class, "c%u", index);
        } else if (index == 1 && container == NO_HEX) {
            sprintf(class, "optional");
        } else {
            sprintf(class, "%s%.0u", hex_names[container], index);
        }
        gr_draw_path(gr, class, phys, 14, true);
    }

    /* Pick a point to use as the centre of the Spectre for labelling */
    centre.x = (log[5].x + log[6].x + log[11].x + log[12].x) / 4;
    centre.y = (log[5].y + log[6].y + log[11].y + log[12].y) / 4;

    /*
     * Label the hex with its index and type.
     */
    if (gr->number_cells && index != (unsigned)-1) {
        char buf[64];
        sprintf(buf, "%u", index);
        gr_draw_text(gr, centre, 1.2, buf);
    }
}

void gr_draw_spectre_from_coords(Graphics *gr, SpectreCoords *sc,
                                 const Point *vertices)
{
    Hex h;
    unsigned index;

    if (!gr)
        return;
    gr_ensure_started(gr);

    if (gr->four_colour) {
        h = NO_HEX;
        if (sc->index == 1)
            index = 3;        /* special colour for odd G1 Spectres */
        else
            index = sc->hex_colour;
    } else if (sc) {
        h = sc->c[0].type;
        index = sc->index;
    } else {
        h = NO_HEX;
        index = -1;
    }
    gr_draw_spectre(gr, h, index, vertices);
}

void gr_draw_extra_edge(Graphics *gr, Point a, Point b)
{
    GrCoords phys[2];

    if (!gr)
        return;
    gr_ensure_started(gr);

    phys[0] = gr_physcoords(gr, a);
    phys[1] = gr_physcoords(gr, b);
    gr_draw_path(gr, "extraedge", phys, 2, false);
}
