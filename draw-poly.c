/*
 * draw-poly.c: Fallback polygon drawing function.
 */

#include <assert.h>

#include "puzzles.h"

struct edge {
    int x1, y1;
    int x2, y2;
    bool active;

    /* whether y1 is a closed endpoint (i.e. this edge should be
     * active when y == y1) */
    bool closed_y1;

    /* (x2 - x1) / (y2 - y1) as 16.16 signed fixed point; precomputed
     * for speed */
    long inverse_slope;
};

#define FRACBITS 16
#define ONEHALF (1 << (FRACBITS-1))

void draw_polygon_fallback(drawing *dr, const int *coords, int npoints,
                           int fillcolour, int outlinecolour)
{
    struct edge *edges;
    int min_y = INT_MAX, max_y = INT_MIN, i, y;
    int n_edges = 0;
    int *intersections;

    if(npoints < 3)
        return;

    if(fillcolour < 0)
        goto draw_outline;

    /* This uses a basic scanline rasterization algorithm for polygon
     * filling. First, an "edge table" is constructed for each pair of
     * neighboring points. Horizontal edges are excluded. Then, the
     * algorithm iterates a horizontal "scan line" over the vertical
     * (Y) extent of the polygon. At each Y level, it maintains a set
     * of "active" edges, which are those which intersect the scan
     * line at the current Y level. The X coordinates where the scan
     * line intersects each active edge are then computed via
     * fixed-point arithmetic and stored. Finally, horizontal lines
     * are drawn between each successive pair of intersection points,
     * in the order of ascending X coordinate. This has the effect of
     * "even-odd" filling when the polygon is self-intersecting.
     *
     * I (vaguely) based this implementation off the slides below:
     *
     * https://www.khoury.northeastern.edu/home/fell/CS4300/Lectures/CS4300F2012-9-ScanLineFill.pdf
     *
     * I'm fairly confident that this current implementation is
     * correct (i.e. draws the right polygon, free from artifacts),
     * but it isn't quite as efficient as it could be. Namely, it
     * currently maintains the active edge set by setting the `active`
     * flag in the `edge` array, which is quite inefficient. Perhaps
     * future optimization could see this replaced with a tree
     * set. Additionally, one could perhaps replace the current linear
     * search for edge endpoints (i.e. the inner loop over `edges`) by
     * sorting the edge table by upper and lower Y coordinate.
     *
     * A final caveat comes from the use of fixed point arithmetic,
     * which is motivated by performance considerations on FPU-less
     * platforms (e.g. most Rockbox devices, and maybe others?). I'm
     * currently using 16 fractional bits to compute the edge
     * intersections, which (in the case of a 32-bit int) limits the
     * acceptable range of coordinates to roughly (-2^14, +2^14). This
     * ought to be acceptable for the forseeable future, but
     * ultra-high DPI screens might one day exceed this. In that case,
     * options include switching to int64_t (but that comes with its
     * own portability headaches), reducing the number of fractional
     * bits, or just giving up and using floating point.
     */

    /* Build edge table from coords. Horizontal edges are filtered
     * out, so n_edges <= n_points in general. */
    edges = smalloc(npoints * sizeof(struct edge));

    for(i = 0; i < npoints; i++) {
        int x1, y1, x2, y2;

        x1 = coords[(2*i+0)];
        y1 = coords[(2*i+1)];
        x2 = coords[(2*i+2) % (npoints * 2)];
        y2 = coords[(2*i+3) % (npoints * 2)];

        if(y1 < min_y)
            min_y = y1;
        if(y1 > max_y)
            max_y = y1;

#define COORD_LIMIT (1<<(sizeof(int)*CHAR_BIT-2 - FRACBITS))
        /* Prevent integer overflow when computing `inverse_slope',
         * which shifts the coordinates left by FRACBITS, and for
         * which we'd like to avoid relying on `long long'. */
	/* If this ever causes issues, see the above comment about
	   possible solutions. */
	assert(x1 < COORD_LIMIT && y1 < COORD_LIMIT);

        /* Only create non-horizontal edges, and require y1 < y2. */
        if(y1 != y2) {
            bool swap = y1 > y2;
            int lower_endpoint = swap ? (i + 1) : i;

            /* Compute index of the point adjacent to lower end of
             * this edge (which is not the upper end of this edge). */
            int lower_neighbor = swap ? (lower_endpoint + 1) % npoints : (lower_endpoint + npoints - 1) % npoints;

            struct edge *edge = edges + (n_edges++);

            edge->active = false;
            edge->x1 = swap ? x2 : x1;
            edge->y1 = swap ? y2 : y1;
            edge->x2 = swap ? x1 : x2;
            edge->y2 = swap ? y1 : y2;
            edge->inverse_slope = ((edge->x2 - edge->x1) << FRACBITS) / (edge->y2 - edge->y1);
            edge->closed_y1 = edge->y1 < coords[2*lower_neighbor+1];
        }
    }

    /* a generous upper bound on number of intersections is n_edges */
    intersections = smalloc(n_edges * sizeof(int));

    for(y = min_y; y <= max_y; y++) {
        int n_intersections = 0;
        for(i = 0; i < n_edges; i++) {
            struct edge *edge = edges + i;
            /* Update active edge set. These conditions are mutually
             * exclusive because of the invariant that y1 < y2. */
            if(edge->y1 + (edge->closed_y1 ? 0 : 1) == y)
                edge->active = true;
            else if(edge->y2 + 1 == y)
                edge->active = false;

            if(edge->active) {
                int x = edges[i].x1;
                x += (edges[i].inverse_slope * (y - edges[i].y1) + ONEHALF) >> FRACBITS;
                intersections[n_intersections++] = x;
            }
        }

        qsort(intersections, n_intersections, sizeof(int), compare_integers);

        assert(n_intersections % 2 == 0);
        assert(n_intersections <= n_edges);

        /* Draw horizontal lines between successive pairs of
         * intersections of the scanline with active edges. */
        for(i = 0; i + 1 < n_intersections; i += 2) {
            draw_line(dr,
                      intersections[i], y,
                      intersections[i+1], y,
                      fillcolour);
        }
    }

    sfree(intersections);
    sfree(edges);

draw_outline:
    assert(outlinecolour >= 0);
    for (i = 0; i < 2 * npoints; i += 2)
        draw_line(dr,
                  coords[i], coords[i+1],
                  coords[(i+2)%(2*npoints)], coords[(i+3)%(2*npoints)],
                  outlinecolour);
}

#ifdef STANDALONE_POLYGON

/*
 * Standalone program to test draw_polygon_fallback(). By default,
 * creates a window and allows clicking points to build a
 * polygon. Optionally, can draw a randomly growing polygon in
 * "screensaver" mode.
 */

#include <SDL.h>

void draw_line(drawing *dr, int x1, int y1, int x2, int y2, int colour)
{
    SDL_Renderer *renderer = GET_HANDLE_AS_TYPE(dr, SDL_Renderer);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define MAX_SCREENSAVER_POINTS 1000

int main(int argc, char *argv[]) {
    SDL_Window* window = NULL;
    SDL_Event event;
    SDL_Renderer *renderer = NULL;
    int running = 1;
    int i;
    drawing dr;
    bool screensaver = false;

    if(argc >= 2) {
	if(!strcmp(argv[1], "--screensaver"))
	    screensaver = true;
	else
	    printf("usage: %s [--screensaver]\n", argv[0]);
    }

    int *poly = NULL;
    int n_points = 0;

    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    /* Create window */
    window = SDL_CreateWindow("Polygon Drawing Test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Create renderer */
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    dr.handle = renderer;

    if(!screensaver)
	printf("Click points in the window to create vertices. Pressing C resets.\n");

    while (running) {
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mouseX = event.button.x;
                    int mouseY = event.button.y;

                    poly = realloc(poly, ++n_points * 2 * sizeof(int));
                    poly[2 * (n_points - 1)] = mouseX;
                    poly[2 * (n_points - 1) + 1] = mouseY;
                }
            }
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_c) {
                    free(poly);
                    poly = NULL;
                    n_points = 0;
                }
            }
        }

	if(screensaver) {
	    poly = realloc(poly, ++n_points * 2 * sizeof(int));
	    poly[2 * (n_points - 1)] = rand() % WINDOW_WIDTH;
	    poly[2 * (n_points - 1) + 1] = rand() % WINDOW_HEIGHT;

	    if(n_points >= MAX_SCREENSAVER_POINTS) {
		free(poly);
		poly = NULL;
		n_points = 0;
	    }
	}

        /* Clear the screen with a black color */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        /* Set draw color to white */
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        draw_polygon_fallback(&dr, poly, n_points, 1, 1);

	SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
	for(i = 0; i < 2*n_points; i+=2)
	    SDL_RenderDrawPoint(renderer, poly[i], poly[i+1]);

        /* Present the back buffer */
        SDL_RenderPresent(renderer);
    }

    /* Clean up and quit SDL */
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
#endif
