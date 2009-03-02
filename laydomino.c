/*
 * laydomino.c: code for performing a domino (2x1 tile) layout of
 * a given area of code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "puzzles.h"

/*
 * This function returns an array size w x h representing a grid:
 * each grid[i] = j, where j is the other end of a 2x1 domino.
 * If w*h is odd, one square will remain referring to itself.
 */

int *domino_layout(int w, int h, random_state *rs)
{
    int *grid, *grid2, *list;
    int wh = w*h;

    /*
     * Allocate space in which to lay the grid out.
     */
    grid = snewn(wh, int);
    grid2 = snewn(wh, int);
    list = snewn(2*wh, int);

    domino_layout_prealloc(w, h, rs, grid, grid2, list);

    sfree(grid2);
    sfree(list);

    return grid;
}

/*
 * As for domino_layout, but with preallocated buffers.
 * grid and grid2 should be size w*h, and list size 2*w*h.
 */
void domino_layout_prealloc(int w, int h, random_state *rs,
                            int *grid, int *grid2, int *list)
{
    int i, j, k, m, wh = w*h, todo, done;

    /*
     * To begin with, set grid[i] = i for all i to indicate
     * that all squares are currently singletons. Later we'll
     * set grid[i] to be the index of the other end of the
     * domino on i.
     */
    for (i = 0; i < wh; i++)
        grid[i] = i;

    /*
     * Now prepare a list of the possible domino locations. There
     * are w*(h-1) possible vertical locations, and (w-1)*h
     * horizontal ones, for a total of 2*wh - h - w.
     *
     * I'm going to denote the vertical domino placement with
     * its top in square i as 2*i, and the horizontal one with
     * its left half in square i as 2*i+1.
     */
    k = 0;
    for (j = 0; j < h-1; j++)
        for (i = 0; i < w; i++)
            list[k++] = 2 * (j*w+i);   /* vertical positions */
    for (j = 0; j < h; j++)
        for (i = 0; i < w-1; i++)
            list[k++] = 2 * (j*w+i) + 1;   /* horizontal positions */
    assert(k == 2*wh - h - w);

    /*
     * Shuffle the list.
     */
    shuffle(list, k, sizeof(*list), rs);

    /*
     * Work down the shuffled list, placing a domino everywhere
     * we can.
     */
    for (i = 0; i < k; i++) {
        int horiz, xy, xy2;

        horiz = list[i] % 2;
        xy = list[i] / 2;
        xy2 = xy + (horiz ? 1 : w);

        if (grid[xy] == xy && grid[xy2] == xy2) {
            /*
             * We can place this domino. Do so.
             */
            grid[xy] = xy2;
            grid[xy2] = xy;
        }
    }

#ifdef GENERATION_DIAGNOSTICS
    printf("generated initial layout\n");
#endif

    /*
     * Now we've placed as many dominoes as we can immediately
     * manage. There will be squares remaining, but they'll be
     * singletons. So loop round and deal with the singletons
     * two by two.
     */
    while (1) {
#ifdef GENERATION_DIAGNOSTICS
        for (j = 0; j < h; j++) {
            for (i = 0; i < w; i++) {
                int xy = j*w+i;
                int v = grid[xy];
                int c = (v == xy+1 ? '[' : v == xy-1 ? ']' :
                         v == xy+w ? 'n' : v == xy-w ? 'U' : '.');
                putchar(c);
            }
            putchar('\n');
        }
        putchar('\n');
#endif

        /*
         * Our strategy is:
         *
         * First find a singleton square.
         *
         * Then breadth-first search out from the starting
         * square. From that square (and any others we reach on
         * the way), examine all four neighbours of the square.
         * If one is an end of a domino, we move to the _other_
         * end of that domino before looking at neighbours
         * again. When we encounter another singleton on this
         * search, stop.
         *
         * This will give us a path of adjacent squares such
         * that all but the two ends are covered in dominoes.
         * So we can now shuffle every domino on the path up by
         * one.
         *
         * (Chessboard colours are mathematically important
         * here: we always end up pairing each singleton with a
         * singleton of the other colour. However, we never
         * have to track this manually, since it's
         * automatically taken care of by the fact that we
         * always make an even number of orthogonal moves.)
         */
        k = 0;
        for (j = 0; j < wh; j++) {
            if (grid[j] == j) {
                k++;
                i = j;          /* start BFS here. */
            }
        }
        if (k == (wh % 2))
            break;              /* if area is even, we have no more singletons;
                                   if area is odd, we have one singleton.
                                   either way, we're done. */

#ifdef GENERATION_DIAGNOSTICS
        printf("starting b.f.s. at singleton %d\n", i);
#endif
        /*
         * Set grid2 to -1 everywhere. It will hold our
         * distance-from-start values, and also our
         * backtracking data, during the b.f.s.
         */
        for (j = 0; j < wh; j++)
            grid2[j] = -1;
        grid2[i] = 0;              /* starting square has distance zero */

        /*
         * Start our to-do list of squares. It'll live in
         * `list'; since the b.f.s can cover every square at
         * most once there is no need for it to be circular.
         * We'll just have two counters tracking the end of the
         * list and the squares we've already dealt with.
         */
        done = 0;
        todo = 1;
        list[0] = i;

        /*
         * Now begin the b.f.s. loop.
         */
        while (done < todo) {
            int d[4], nd, x, y;

            i = list[done++];

#ifdef GENERATION_DIAGNOSTICS
            printf("b.f.s. iteration from %d\n", i);
#endif
            x = i % w;
            y = i / w;
            nd = 0;
            if (x > 0)
                d[nd++] = i - 1;
            if (x+1 < w)
                d[nd++] = i + 1;
            if (y > 0)
                d[nd++] = i - w;
            if (y+1 < h)
                d[nd++] = i + w;
            /*
             * To avoid directional bias, process the
             * neighbours of this square in a random order.
             */
            shuffle(d, nd, sizeof(*d), rs);

            for (j = 0; j < nd; j++) {
                k = d[j];
                if (grid[k] == k) {
#ifdef GENERATION_DIAGNOSTICS
                    printf("found neighbouring singleton %d\n", k);
#endif
                    grid2[k] = i;
                    break;         /* found a target singleton! */
                }

                /*
                 * We're moving through a domino here, so we
                 * have two entries in grid2 to fill with
                 * useful data. In grid[k] - the square
                 * adjacent to where we came from - I'm going
                 * to put the address _of_ the square we came
                 * from. In the other end of the domino - the
                 * square from which we will continue the
                 * search - I'm going to put the distance.
                 */
                m = grid[k];

                if (grid2[m] < 0 || grid2[m] > grid2[i]+1) {
#ifdef GENERATION_DIAGNOSTICS
                    printf("found neighbouring domino %d/%d\n", k, m);
#endif
                    grid2[m] = grid2[i]+1;
                    grid2[k] = i;
                    /*
                     * And since we've now visited a new
                     * domino, add m to the to-do list.
                     */
                    assert(todo < wh);
                    list[todo++] = m;
                }
            }

            if (j < nd) {
                i = k;
#ifdef GENERATION_DIAGNOSTICS
                printf("terminating b.f.s. loop, i = %d\n", i);
#endif
                break;
            }

            i = -1;                /* just in case the loop terminates */
        }

        /*
         * We expect this b.f.s. to have found us a target
         * square.
         */
        assert(i >= 0);

        /*
         * Now we can follow the trail back to our starting
         * singleton, re-laying dominoes as we go.
         */
        while (1) {
            j = grid2[i];
            assert(j >= 0 && j < wh);
            k = grid[j];

            grid[i] = j;
            grid[j] = i;
#ifdef GENERATION_DIAGNOSTICS
            printf("filling in domino %d/%d (next %d)\n", i, j, k);
#endif
            if (j == k)
                break;             /* we've reached the other singleton */
            i = k;
        }
#ifdef GENERATION_DIAGNOSTICS
        printf("fixup path completed\n");
#endif
    }
}

/* vim: set shiftwidth=4 :set textwidth=80: */

