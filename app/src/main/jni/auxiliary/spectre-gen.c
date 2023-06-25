/*
 * Generate the lookup tables used by the Spectre tiling.
 */

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "puzzles.h"
#include "tree234.h"
#include "spectre-internal.h"
#include "spectre-tables-manual.h"
#include "spectre-tables-extra.h"
#include "spectre-help.h"

struct HexData {
    const Hex *subhexes;
    const unsigned *orientations;
    const int *edges;
    Point hex_outline_start, hex_outline_direction;
    unsigned spectre_outline_start_spec, spectre_outline_start_vertex;
};

static const struct HexData hexdata[] = {
    #define HEXDATA_ENTRY(x) { subhexes_##x, orientations_##x, edges_##x, \
            HEX_OUTLINE_START_##x, SPEC_OUTLINE_START_##x },
    HEX_LETTERS(HEXDATA_ENTRY)
    #undef HEXDATA_ENTRY
};

/*
 * Store information about an edge of the hexagonal tiling.
 */
typedef struct EdgeData {
    /* Edges are regarded as directed, so that we can store
     * information separately about what's on each side of one. The
     * names 'start' and 'finish' indicate a direction of travel,
     * which is taken to be anticlockwise around a hexagon, i.e. if
     * you walk from 'start' to 'finish' then the hexagon in question
     * is the one on your left. */
    Point start, finish;

    /* Whether this edge is internal (i.e. owned by a hexagon). */
    bool internal;

    /*
     * High- and low-order parts of the edge identity.
     *
     * If the edge is internal, then 'hi' indexes the hexagon it's an
     * edge of, and 'lo' identifies one of its edges.
     *
     * If it's external, then 'hi' is the index of the edge segment
     * corresponding to a particular edge of the superhex, and 'lo'
     * the sub-index within that segment.
     */
    unsigned hi, lo;
} EdgeData;

static int edge_cmp(void *av, void *bv)
{
    const EdgeData *a = (const EdgeData *)av;
    const EdgeData *b = (const EdgeData *)bv;
    size_t i;

    for (i = 0; i < 4; i++) {
        if (a->start.coeffs[i] < b->start.coeffs[i])
            return -1;
        if (a->start.coeffs[i] > b->start.coeffs[i])
            return +1;
    }
    for (i = 0; i < 4; i++) {
        if (a->finish.coeffs[i] < b->finish.coeffs[i])
            return -1;
        if (a->finish.coeffs[i] > b->finish.coeffs[i])
            return +1;
    }
    return 0;
}

static void lay_out_hexagons(Hex h, Graphics *gr, FILE *hdr)
{
    size_t i, j;
    tree234 *edge_map = newtree234(edge_cmp);
    EdgeData *edge;
    EdgeData *intmap[48], *extmap[22];
    unsigned edgestarts[7];
    const struct HexData *hd = h == NO_HEX ? NULL : &hexdata[h];

    /*
     * Iterate over all hexagons and enter their edges into the edge
     * map.
     */
    for (i = 0; i < (h == NO_HEX ? 8 : num_subhexes(h)); i++) {
        Point centre = hex_centres[i];
        Point vrel = {{ -2, 0, 4, 0 }};
        Point vertices[6];

        if (hd)
            vrel = point_mul(vrel, point_rot(2*hd->orientations[i]));
        for (j = 0; j < 6; j++) {
            Point vrelnext = point_mul(vrel, point_rot(2));

            edge = snew(EdgeData);
            edge->start = point_add(centre, vrel);
            edge->finish = point_add(centre, vrelnext);
            edge->internal = true;
            edge->hi = i;
            edge->lo = j;
            add234(edge_map, edge);
            intmap[6*i + j] = edge;

            vertices[j] = edge->start;

            vrel = vrelnext;
        }

        gr_draw_hex(gr, gr->jigsaw_mode ? -1 : i,
                    hd ? hd->subhexes[i] : NO_HEX, vertices);
    }

    /*
     * Trace round the exterior outline of the hex expansion,
     * following the list of edge types.
     */
    if (hd) {
        Point pos, dir;
        size_t mappos = 0;

        pos = hd->hex_outline_start;
        dir = hd->hex_outline_direction;

        for (i = 0; i < 6; i++) {
            int edge_type = hd->edges[i];
            int sign = edge_type < 0 ? -1 : +1;
            const int *edge_shape = hex_edge_shapes[abs(edge_type)];
            size_t len = hex_edge_lengths[abs(edge_type)];
            size_t index = sign < 0 ? len-2 : 0;

            if (gr->vertex_blobs)
                gr_draw_blob(gr, (i == 0 ? "startpoint" : "edgesep"),
                             gr_logcoords(pos), (i == 0 ? 0.6 : 0.3));

            edgestarts[i] = mappos;

            for (j = 0; j < len; j++) {
                Point posnext = point_add(pos, dir);
                if (j < len-1) {
                    dir = point_mul(dir, point_rot(sign * edge_shape[index]));
                    index += sign;
                }

                edge = snew(EdgeData);
                edge->start = pos;
                edge->finish = posnext;
                edge->internal = false;
                edge->hi = i;
                edge->lo = j;
                add234(edge_map, edge);

                assert(mappos < lenof(extmap));
                extmap[mappos++] = edge;

                pos = posnext;
            }

            /*
             * In the hex expansion, every pair of edges meet at a
             * 60-degree left turn.
             */
            dir = point_mul(dir, point_rot(-2));
        }

        edgestarts[i] = mappos;        /* record end position */

        for (i = 0; i < 4; i++)
            assert(pos.coeffs[i] == hd->hex_outline_start.coeffs[i]);
    }

    /*
     * Draw the labels on the edges.
     */
    if (gr->number_edges) {
        for (i = 0; (edge = index234(edge_map, i)) != NULL; i++) {
            char buf[64];
            double textheight = 0.8, offset = textheight * 0.2;
            GrCoords start = gr_logcoords(edge->start);
            GrCoords finish = gr_logcoords(edge->finish);
            GrCoords len = { finish.x - start.x, finish.y - start.y };
            GrCoords perp = { -len.y, +len.x };
            GrCoords mid = { (start.x+finish.x)/2, (start.y+finish.y)/2 };

            if (edge->internal) {
                sprintf(buf, "%u", edge->lo);
            } else {
                sprintf(buf, "%u.%u", edge->lo, edge->hi);
                offset = textheight * 0.3;
            }

            {
                GrCoords pos = {
                    mid.x + offset * perp.x,
                    mid.y + offset * perp.y,
                };
                gr_draw_text(gr, pos, textheight, buf);
            }
        }
    }

    /*
     * Write out C array declarations for the machine-readable version
     * of the maps we just generated.
     */
    if (hdr) {
        fprintf(hdr, "static const struct MapEntry hexmap_%s[] = {\n",
                hex_names[h]);
        for (i = 0; i < 6 * num_subhexes(h); i++) {
            EdgeData *our_edge = intmap[i];
            EdgeData key, *rev_edge;
            key.finish = our_edge->start;
            key.start = our_edge->finish;
            rev_edge = find234(edge_map, &key, NULL);
            assert(rev_edge);
            fprintf(hdr, "    { %-6s %u, %u }, /* edge %u of hex %u (%s) */\n",
                    rev_edge->internal ? "true," : "false,",
                    rev_edge->hi, rev_edge->lo,
                    our_edge->lo, our_edge->hi,
                    hex_names[hd->subhexes[our_edge->hi]]);
        }
        fprintf(hdr, "};\n");

        fprintf(hdr, "static const struct MapEdge hexedges_%s[] = {\n",
                hex_names[h]);
        for (i = 0; i < 6; i++)
            fprintf(hdr, "    { %2u, %u },\n", edgestarts[i],
                    edgestarts[i+1] - edgestarts[i]);
        fprintf(hdr, "};\n");

        fprintf(hdr, "static const struct MapEntry hexin_%s[] = {\n",
                hex_names[h]);
        for (i = 0; i < edgestarts[6]; i++) {
            EdgeData *our_edge = extmap[i];
            EdgeData key, *rev_edge;
            key.finish = our_edge->start;
            key.start = our_edge->finish;
            rev_edge = find234(edge_map, &key, NULL);
            assert(rev_edge);
            fprintf(hdr, "    { %-6s %u, %u }, /* subedge %u of edge %u */\n",
                    rev_edge->internal ? "true," : "false,",
                    rev_edge->hi, rev_edge->lo,
                    our_edge->lo, our_edge->hi);
        }
        fprintf(hdr, "};\n");
    }

    while ((edge = delpos234(edge_map, 0)) != NULL)
        sfree(edge);
    freetree234(edge_map);
}

static void lay_out_spectres(Hex h, Graphics *gr, FILE *hdr)
{
    size_t i, j;
    tree234 *edge_map = newtree234(edge_cmp);
    EdgeData *edge;
    EdgeData *intmap[28], *extmap[24];
    Point vertices[28];
    unsigned edgestarts[7];
    const struct HexData *hd = (h == NO_HEX ? NULL : &hexdata[h]);

    /*
     * Iterate over the Spectres in a hex (usually only one), and enter
     * their edges into the edge map.
     */
    for (i = 0; i < (h == NO_HEX ? 2 : num_spectres(h)); i++) {
        Point start = {{ 0, 0, 0, 0 }};
        Point pos = start;
        Point diag = {{ 2, 0, 0, 2 }};
        Point dir = point_mul(diag, point_rot(5));

        /*
         * Usually the single Spectre in each map is oriented in the
         * same place. For spectre #1 in the G map, however, we orient
         * it manually in a different location. (There's no point
         * making an organised lookup table for just this one
         * exceptional case.)
         */
        if (i == 1) {
            Point unusual_start = {{ 2, 6, 2, 0 }};
            pos = unusual_start;
            dir = point_mul(dir, point_rot(+1));
        }

        for (j = 0; j < 14; j++) {
            edge = snew(EdgeData);
            edge->start = pos;
            edge->finish = point_add(pos, dir);
            edge->internal = true;
            edge->hi = i;
            edge->lo = j;
            add234(edge_map, edge);
            intmap[14*i + j] = edge;

            vertices[14*i + j] = edge->start;

            pos = edge->finish;
            dir = point_mul(dir, point_rot(spectre_angles[(j+1) % 14]));
        }

        gr_draw_spectre(gr, h, i, vertices + 14*i);
    }

    /*
     * Trace round the exterior outline of the hex expansion,
     * following the list of edge types. Due to the confusing
     * reflection of all the expansions, we end up doing this in the
     * reverse order to the hexes code above.
     */
    if (hd) {
        Point start, pos, dir;
        size_t mappos = lenof(extmap);

        start = pos = vertices[14 * hd->spectre_outline_start_spec +
                               hd->spectre_outline_start_vertex];

        edgestarts[6] = mappos;

        for (i = 0; i < 6; i++) {
            int edge_type = hd->edges[5-i];
            int sign = edge_type < 0 ? -1 : +1;
            const int *edge_shape = spec_edge_shapes[abs(edge_type)];
            size_t len = spec_edge_lengths[abs(edge_type)];
            size_t index = sign < 0 ? len-2 : 0;

            if (gr->vertex_blobs)
                gr_draw_blob(gr, (i == 0 ? "startpoint" : "edgesep"),
                             gr_logcoords(pos), (i == 0 ? 0.6 : 0.3));

            if (h == HEX_S && i >= 4) {
                /*
                 * Two special cases
                 */
                if (i == 4)
                    /* leave dir from last time */;
                else
                    dir = point_mul(dir, point_rot(6)); /* reverse */
            } else {
                /*
                 * Determine the direction of the first sub-edge of
                 * this edge expansion, by iterating over all the
                 * edges in edge_map starting at this point and
                 * finding one whose reverse isn't in the map (hence,
                 * it's an exterior edge).
                 */
                EdgeData dummy, *iter, *found = NULL;
                dummy.start = pos;
                for (j = 0; j < 4; j++)
                    dummy.finish.coeffs[j] = INT_MIN;
                for (iter = findrel234(edge_map, &dummy, NULL, REL234_GE);
                     iter != NULL && point_equal(iter->start, pos);
                     iter = findrel234(edge_map, iter, NULL, REL234_GT)) {
                    EdgeData *rev;

                    dummy.finish = iter->start;
                    dummy.start = iter->finish;
                    rev = find234(edge_map, &dummy, NULL);
                    if (!rev) {
                        found = iter;
                        break;
                    }
                }

                assert(found);
                dir = point_sub(found->finish, found->start);
            }

            for (j = 0; j < len; j++) {
                Point posnext = point_add(pos, dir);
                if (j < len-1) {
                    dir = point_mul(dir, point_rot(sign * edge_shape[index]));
                    index += sign;
                }

                edge = snew(EdgeData);
                edge->start = posnext;
                edge->finish = pos;
                edge->internal = false;
                edge->hi = 5-i;
                edge->lo = len-1-j;
                add234(edge_map, edge);

                assert(mappos > 0);
                extmap[--mappos] = edge;

                pos = posnext;
            }

            edgestarts[5-i] = mappos;
        }

        assert(point_equal(pos, start));
    }

    /*
     * Draw the labels on the edges.
     */
    if (gr->number_edges) {
        for (i = 0; (edge = index234(edge_map, i)) != NULL; i++) {
            char buf[64];
            double textheight = 0.8, offset = textheight * 0.2;
            GrCoords start = gr_logcoords(edge->start);
            GrCoords finish = gr_logcoords(edge->finish);
            GrCoords len = { finish.x - start.x, finish.y - start.y };
            GrCoords perp = { +len.y, -len.x };
            GrCoords mid = { (start.x+finish.x)/2, (start.y+finish.y)/2 };

            if (edge->internal) {
                sprintf(buf, "%u", edge->lo);
            } else {
                sprintf(buf, "%u.%u", edge->lo, edge->hi);
                textheight = 0.6;
            }
            if (strlen(buf) > 1)
                offset = textheight * 0.35;

            {
                GrCoords pos = {
                    mid.x + offset * perp.x,
                    mid.y + offset * perp.y,
                };
                gr_draw_text(gr, pos, textheight, buf);
            }
        }
    }

    /*
     * Write out C array declarations for the machine-readable version
     * of the maps we just generated.
     *
     * Also, because it's easier than having a whole extra iteration,
     * draw lines for the extraordinary edges outside the S diagram.
     */
    if (hdr) {
        fprintf(hdr, "static const struct MapEntry specmap_%s[] = {\n",
                hex_names[h]);
        for (i = 0; i < 14 * num_spectres(h); i++) {
            EdgeData *our_edge = intmap[i];
            EdgeData key, *rev_edge;
            key.finish = our_edge->start;
            key.start = our_edge->finish;
            rev_edge = find234(edge_map, &key, NULL);
            assert(rev_edge);
            fprintf(hdr, "    { %-6s %u, %2u }, /* edge %2u of Spectre %u */\n",
                    rev_edge->internal ? "true," : "false,",
                    rev_edge->hi, rev_edge->lo,
                    our_edge->lo, our_edge->hi);
        }
        fprintf(hdr, "};\n");

        fprintf(hdr, "static const struct MapEdge specedges_%s[] = {\n",
                hex_names[h]);
        for (i = 0; i < 6; i++)
            fprintf(hdr, "    { %2u, %u },\n", edgestarts[i] - edgestarts[0],
                    edgestarts[i+1] - edgestarts[i]);
        fprintf(hdr, "};\n");

        fprintf(hdr, "static const struct MapEntry specin_%s[] = {\n",
                hex_names[h]);
        for (i = edgestarts[0]; i < edgestarts[6]; i++) {
            EdgeData *our_edge = extmap[i];
            EdgeData key, *rev_edge;
            key.finish = our_edge->start;
            key.start = our_edge->finish;
            rev_edge = find234(edge_map, &key, NULL);
            assert(rev_edge);
            fprintf(hdr, "    { %-6s %u, %2u }, /* subedge %u of edge %u */\n",
                    rev_edge->internal ? "true," : "false,",
                    rev_edge->hi, rev_edge->lo,
                    our_edge->lo, our_edge->hi);

            if (!our_edge->internal && !rev_edge->internal)
                gr_draw_extra_edge(gr, key.start, key.finish);
        }
        fprintf(hdr, "};\n");
    }

    while ((edge = delpos234(edge_map, 0)) != NULL)
        sfree(edge);
    freetree234(edge_map);
}

static void draw_base_hex(Hex h, Graphics *gr)
{
    size_t i;
    Point vertices[6];

    /*
     * Plot the points of the hex.
     */
    for (i = 0; i < 6; i++) {
        Point startvertex = {{ -2, 0, 4, 0 }};
        vertices[i] = point_mul(startvertex, point_rot(2*i));
    }

    /*
     * Draw the hex itself.
     */
    gr_draw_hex(gr, -1, h, vertices);

    if (gr->vertex_blobs) {
        /*
         * Draw edge-division blobs on all vertices, to match the ones on
         * the expansion diagrams.
         */
        for (i = 0; i < 6; i++) {
            gr_draw_blob(gr, (i == 0 ? "startpoint" : "edgesep"),
                         gr_logcoords(vertices[i]), (i == 0 ? 0.6 : 0.3));
        }
    }

    if (gr->number_edges) {
        /*
         * Draw the labels on its edges.
         */
        for (i = 0; i < 6; i++) {
            char buf[64];
            double textheight = 0.8, offset = textheight * 0.2;
            GrCoords start = gr_logcoords(vertices[i]);
            GrCoords finish = gr_logcoords(vertices[(i+1) % 6]);
            GrCoords len = { finish.x - start.x, finish.y - start.y };
            GrCoords perp = { -len.y, +len.x };
            GrCoords mid = { (start.x+finish.x)/2, (start.y+finish.y)/2 };

            sprintf(buf, "%zu", i);

            {
                GrCoords pos = {
                    mid.x + offset * perp.x,
                    mid.y + offset * perp.y,
                };
                gr_draw_text(gr, pos, textheight, buf);
            }
        }
    }
}

static void draw_one_spectre(Graphics *gr)
{
    size_t i, j;
    Point vertices[14];

    {
        Point start = {{ 0, 0, 0, 0 }};
        Point pos = start;
        Point diag = {{ 2, 0, 0, 2 }};
        Point dir = point_mul(diag, point_rot(9));

        for (j = 0; j < 14; j++) {
            vertices[j] = pos;
            pos = point_add(pos, dir);
            dir = point_mul(dir, point_rot(spectre_angles[(j+1) % 14]));
        }

        gr_draw_spectre(gr, NO_HEX, -1, vertices);
    }

    /*
     * Draw the labels on the edges.
     */
    if (gr->number_edges) {
        for (i = 0; i < 14; i++) {
            char buf[64];
            double textheight = 0.8, offset = textheight * 0.2;
            GrCoords start = gr_logcoords(vertices[i]);
            GrCoords finish = gr_logcoords(vertices[(i+1) % 14]);
            GrCoords len = { finish.x - start.x, finish.y - start.y };
            GrCoords perp = { +len.y, -len.x };
            GrCoords mid = { (start.x+finish.x)/2, (start.y+finish.y)/2 };

            sprintf(buf, "%zu", i);
            if (strlen(buf) > 1)
                offset = textheight * 0.35;

            {
                GrCoords pos = {
                    mid.x + offset * perp.x,
                    mid.y + offset * perp.y,
                };
                gr_draw_text(gr, pos, textheight, buf);
            }
        }
    }

}

static void make_parent_tables(FILE *fp)
{
    size_t i, j, k;

    for (i = 0; i < 9; i++) {
        fprintf(fp, "static const struct Possibility poss_%s[] = {\n",
                hex_names[i]);
        for (j = 0; j < 9; j++) {
            for (k = 0; k < num_subhexes(j); k++) {
                if (hexdata[j].subhexes[k] == i) {
                    fprintf(fp, "    { HEX_%s, %zu, PROB_%s },\n",
                            hex_names[j], k, hex_names[j]);
                }
            }
        }
        fprintf(fp, "};\n");
    }

    fprintf(fp, "static const struct Possibility poss_spectre[] = {\n");
    for (j = 0; j < 9; j++) {
        for (k = 0; k < num_spectres(j); k++) {
            fprintf(fp, "    { HEX_%s, %zu, PROB_%s },\n",
                    hex_names[j], k, hex_names[j]);
        }
    }
    fprintf(fp, "};\n");
}

int main(void)
{
    size_t i;
    FILE *fp = fopen("spectre-tables-auto.h", "w");
    fprintf(fp,
            "/*\n"
            " * Autogenerated transition tables for the Spectre tiling.\n"
            " * Generated by auxiliary/spectre-gen.c.\n"
            " */\n\n");

    for (i = 0; i < 9; i++) {
        char buf[64];
        sprintf(buf, "hexmap_%s.svg", hex_names[i]);
        Graphics *gr = gr_new(buf, -11, +11, -20, +4.5, 13);
        lay_out_hexagons(i, gr, fp);
        gr_free(gr);
    }
    for (i = 0; i < 9; i++) {
        char buf[64];
        sprintf(buf, "specmap_%s.svg", hex_names[i]);
        Graphics *gr = gr_new(buf, (i == HEX_S ? -14 : -11.5),
                              (i == HEX_G ? +10 : 0.5),
                              -2, +12, 15);
        lay_out_spectres(i, gr, fp);
        gr_free(gr);
    }
    for (i = 0; i < 9; i++) {
        char buf[64];
        sprintf(buf, "basehex_%s.svg", hex_names[i]);
        Graphics *gr = gr_new(buf, -4, +4, -4.2, +4.5, 15);
        draw_base_hex(i, gr);
        gr_free(gr);
    }
    for (i = 0; i < 9; i++) {
        char buf[64];
        sprintf(buf, "jigsawhex_%s.svg", hex_names[i]);
        Graphics *gr = gr_new(buf, -4, +4, -4.2, +4.5, 20);
        gr->jigsaw_mode = true;
        gr->vertex_blobs = false;
        gr->number_edges = false;
        draw_base_hex(i, gr);
        gr_free(gr);
    }
    {
        Graphics *gr = gr_new("basehex_null.svg", -4, +4, -4.2, +4.5, 20);
        gr->vertex_blobs = false;
        draw_base_hex(NO_HEX, gr);
        gr_free(gr);
    }
    {
        Graphics *gr = gr_new("basespec_null.svg", -7, +6, -14, +1, 15);
        gr->vertex_blobs = false;
        draw_one_spectre(gr);
        gr_free(gr);
    }
    {
        Graphics *gr = gr_new("hexmap_null.svg", -11, +11, -20, +4.5, 10);
        gr->vertex_blobs = false;
        gr->number_edges = false;
        gr->hex_arrows = false;
        lay_out_hexagons(NO_HEX, gr, NULL);
        gr_free(gr);
    }
    {
        Graphics *gr = gr_new("specmap_null.svg", -11.5, +10, -2, +12, 15);
        gr->vertex_blobs = false;
        gr->number_edges = false;
        gr->hex_arrows = false;
        lay_out_spectres(NO_HEX, gr, NULL);
        gr_free(gr);
    }
    for (i = 0; i < 2; i++) {
        char buf[64];
        sprintf(buf, "jigsawexpand_%s.svg", hex_names[i]);
        Graphics *gr = gr_new(buf, -11, +11, -20, +4.5, 10);
        gr->jigsaw_mode = true;
        gr->vertex_blobs = false;
        gr->number_edges = false;
        lay_out_hexagons(i, gr, fp);
        gr_free(gr);
    }
    make_parent_tables(fp);
    fclose(fp);
    return 0;
}
