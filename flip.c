/*
 * flip.c: Puzzle involving lighting up all the squares on a grid,
 * where each click toggles an overlapping set of lights.
 */

/*
 * TODO:
 * 
 *  - `Solve' could mark the squares you must click to solve
 *     + infrastructure change: this would mean the Solve operation
 * 	 must receive the current game_state as well as the initial
 * 	 one, which I've been wondering about for a while
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"

enum {
    COL_BACKGROUND,
    COL_WRONG,
    COL_RIGHT,
    COL_GRID,
    COL_DIAG,
    NCOLOURS
};

#define PREFERRED_TILE_SIZE 48
#define TILE_SIZE (ds->tilesize)
#define BORDER    (TILE_SIZE / 2)
#define COORD(x)  ( (x) * TILE_SIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILE_SIZE) / TILE_SIZE - 1 )

#define ANIM_TIME 0.25F
#define FLASH_FRAME 0.07F

/*
 * Possible ways to decide which lights are toggled by each click.
 * Essentially, each of these describes a means of inventing a
 * matrix over GF(2).
 */
enum {
    CROSSES, RANDOM
};

struct game_params {
    int w, h;
    int matrix_type;
};

/*
 * This structure is shared between all the game_states describing
 * a particular game, so it's reference-counted.
 */
struct matrix {
    int refcount;
    unsigned char *matrix;             /* array of (w*h) by (w*h) */
};

struct game_state {
    int w, h;
    int moves, completed;
    unsigned char *grid;               /* array of w*h */
    struct matrix *matrix;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 5;
    ret->matrix_type = CROSSES;

    return ret;
}

static const struct game_params flip_presets[] = {
    {3, 3, CROSSES},
    {4, 4, CROSSES},
    {5, 5, CROSSES},
    {3, 3, RANDOM},
    {4, 4, RANDOM},
    {5, 5, RANDOM},
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char str[80];

    if (i < 0 || i >= lenof(flip_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = flip_presets[i];

    sprintf(str, "%dx%d %s", ret->w, ret->h,
            ret->matrix_type == CROSSES ? "Crosses" : "Random");

    *name = dupstr(str);
    *params = ret;
    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    ret->w = ret->h = atoi(string);
    while (*string && isdigit(*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
        while (*string && isdigit(*string)) string++;
    }
    if (*string == 'r') {
        string++;
        ret->matrix_type = RANDOM;
    } else if (*string == 'c') {
        string++;
        ret->matrix_type = CROSSES;
    }
}

static char *encode_params(game_params *params, int full)
{
    char data[256];

    sprintf(data, "%dx%d%s", params->w, params->h,
            !full ? "" : params->matrix_type == CROSSES ? "c" : "r");

    return dupstr(data);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret = snewn(4, config_item);
    char buf[80];

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = "Shape type";
    ret[2].type = C_CHOICES;
    ret[2].sval = ":Crosses:Random";
    ret[2].ival = params->matrix_type;

    ret[3].name = NULL;
    ret[3].type = C_END;
    ret[3].sval = NULL;
    ret[3].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
    ret->matrix_type = cfg[2].ival;

    return ret;
}

static char *validate_params(game_params *params)
{
    if (params->w <= 0 || params->h <= 0)
        return "Width and height must both be greater than zero";
    return NULL;
}

static char *encode_bitmap(unsigned char *bmp, int len)
{
    int slen = (len + 3) / 4;
    char *ret;
    int i;

    ret = snewn(slen + 1, char);
    for (i = 0; i < slen; i++) {
        int j, v;
        v = 0;
        for (j = 0; j < 4; j++)
            if (i*4+j < len && bmp[i*4+j])
                v |= 8 >> j;
        ret[i] = "0123456789abcdef"[v];
    }
    ret[slen] = '\0';
    return ret;
}

static void decode_bitmap(unsigned char *bmp, int len, char *hex)
{
    int slen = (len + 3) / 4;
    int i;

    for (i = 0; i < slen; i++) {
        int j, v, c = hex[i];
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else
            v = 0;                     /* shouldn't happen */
        for (j = 0; j < 4; j++) {
            if (i*4+j < len) {
                if (v & (8 >> j))
                    bmp[i*4+j] = 1;
                else
                    bmp[i*4+j] = 0;
            }
        }
    }
}

/*
 * Structure used during random matrix generation, and a compare
 * function to permit storage in a tree234.
 */
struct sq {
    int cx, cy;                        /* coords of click square */
    int x, y;                          /* coords of output square */
    /*
     * Number of click squares which currently affect this output
     * square.
     */
    int coverage;
    /*
     * Number of output squares currently affected by this click
     * square.
     */
    int ominosize;
};
#define SORT(field) do { \
    if (a->field < b->field) \
        return -1; \
    else if (a->field > b->field) \
        return +1; \
} while (0)
/*
 * Compare function for choosing the next square to add. We must
 * sort by coverage, then by omino size, then everything else.
 */
static int sqcmp_pick(void *av, void *bv)
{
    struct sq *a = (struct sq *)av;
    struct sq *b = (struct sq *)bv;
    SORT(coverage);
    SORT(ominosize);
    SORT(cy);
    SORT(cx);
    SORT(y);
    SORT(x);
    return 0;
}
/*
 * Compare function for adjusting the coverage figures after a
 * change. We sort first by coverage and output square, then by
 * everything else.
 */
static int sqcmp_cov(void *av, void *bv)
{
    struct sq *a = (struct sq *)av;
    struct sq *b = (struct sq *)bv;
    SORT(coverage);
    SORT(y);
    SORT(x);
    SORT(ominosize);
    SORT(cy);
    SORT(cx);
    return 0;
}
/*
 * Compare function for adjusting the omino sizes after a change.
 * We sort first by omino size and input square, then by everything
 * else.
 */
static int sqcmp_osize(void *av, void *bv)
{
    struct sq *a = (struct sq *)av;
    struct sq *b = (struct sq *)bv;
    SORT(ominosize);
    SORT(cy);
    SORT(cx);
    SORT(coverage);
    SORT(y);
    SORT(x);
    return 0;
}
static void addsq(tree234 *t, int w, int h, int cx, int cy,
                  int x, int y, unsigned char *matrix)
{
    int wh = w * h;
    struct sq *sq;
    int i;

    if (x < 0 || x >= w || y < 0 || y >= h)
        return;
    if (abs(x-cx) > 1 || abs(y-cy) > 1)
        return;
    if (matrix[(cy*w+cx) * wh + y*w+x])
        return;

    sq = snew(struct sq);
    sq->cx = cx;
    sq->cy = cy;
    sq->x = x;
    sq->y = y;
    sq->coverage = sq->ominosize = 0;
    for (i = 0; i < wh; i++) {
        if (matrix[i * wh + y*w+x])
            sq->coverage++;
        if (matrix[(cy*w+cx) * wh + i])
            sq->ominosize++;
    }

    if (add234(t, sq) != sq)
        sfree(sq);                     /* already there */
}
static void addneighbours(tree234 *t, int w, int h, int cx, int cy,
                          int x, int y, unsigned char *matrix)
{
    addsq(t, w, h, cx, cy, x-1, y, matrix);
    addsq(t, w, h, cx, cy, x+1, y, matrix);
    addsq(t, w, h, cx, cy, x, y-1, matrix);
    addsq(t, w, h, cx, cy, x, y+1, matrix);
}

static char *new_game_desc(game_params *params, random_state *rs,
			   game_aux_info **aux, int interactive)
{
    int w = params->w, h = params->h, wh = w * h;
    int i, j;
    unsigned char *matrix, *grid;
    char *mbmp, *gbmp, *ret;

    matrix = snewn(wh * wh, unsigned char);
    grid = snewn(wh, unsigned char);

    /*
     * First set up the matrix.
     */
    switch (params->matrix_type) {
      case CROSSES:
        for (i = 0; i < wh; i++) {
            int ix = i % w, iy = i / w;
            for (j = 0; j < wh; j++) {
                int jx = j % w, jy = j / w;
                if (abs(jx - ix) + abs(jy - iy) <= 1)
                    matrix[i*wh+j] = 1;
                else
                    matrix[i*wh+j] = 0;
            }
        }
        break;
      case RANDOM:
        while (1) {
            tree234 *pick, *cov, *osize;
            int limit;

            pick = newtree234(sqcmp_pick);
            cov = newtree234(sqcmp_cov);
            osize = newtree234(sqcmp_osize);

            memset(matrix, 0, wh * wh);
            for (i = 0; i < wh; i++) {
                matrix[i*wh+i] = 1;
            }

            for (i = 0; i < wh; i++) {
                int ix = i % w, iy = i / w;
                addneighbours(pick, w, h, ix, iy, ix, iy, matrix);
                addneighbours(cov, w, h, ix, iy, ix, iy, matrix);
                addneighbours(osize, w, h, ix, iy, ix, iy, matrix);
            }

            /*
             * Repeatedly choose a square to add to the matrix,
             * until we have enough. I'll arbitrarily choose our
             * limit to be the same as the total number of set bits
             * in the crosses matrix.
             */
            limit = 4*wh - 2*(w+h);    /* centre squares already present */

            while (limit-- > 0) {
                struct sq *sq, *sq2, sqlocal;
                int k;

                /*
                 * Find the lowest element in the pick tree.
                 */
                sq = index234(pick, 0);

                /*
                 * Find the highest element with the same coverage
                 * and omino size, by setting all other elements to
                 * lots.
                 */
                sqlocal = *sq;
                sqlocal.cx = sqlocal.cy = sqlocal.x = sqlocal.y = wh;
                sq = findrelpos234(pick, &sqlocal, NULL, REL234_LT, &k);
                assert(sq != 0);

                /*
                 * Pick at random from all elements up to k of the
                 * pick tree.
                 */
                k = random_upto(rs, k+1);
                sq = delpos234(pick, k);
                del234(cov, sq);
                del234(osize, sq);

                /*
                 * Add this square to the matrix.
                 */
                matrix[(sq->cy * w + sq->cx) * wh + (sq->y * w + sq->x)] = 1;

                /*
                 * Correct the matrix coverage field of any sq
                 * which points at this output square.
                 */
                sqlocal = *sq;
                sqlocal.cx = sqlocal.cy = sqlocal.ominosize = -1;
                while ((sq2 = findrel234(cov, &sqlocal, NULL,
                                         REL234_GT)) != NULL &&
                       sq2->coverage == sq->coverage &&
                       sq2->x == sq->x && sq2->y == sq->y) {
                    del234(pick, sq2);
                    del234(cov, sq2);
                    del234(osize, sq2);
                    sq2->coverage++;
                    add234(pick, sq2);
                    add234(cov, sq2);
                    add234(osize, sq2);
                }

                /*
                 * Correct the omino size field of any sq which
                 * points at this input square.
                 */
                sqlocal = *sq;
                sqlocal.x = sqlocal.y = sqlocal.coverage = -1;
                while ((sq2 = findrel234(osize, &sqlocal, NULL,
                                         REL234_GT)) != NULL &&
                       sq2->ominosize == sq->ominosize &&
                       sq2->cx == sq->cx && sq2->cy == sq->cy) {
                    del234(pick, sq2);
                    del234(cov, sq2);
                    del234(osize, sq2);
                    sq2->ominosize++;
                    add234(pick, sq2);
                    add234(cov, sq2);
                    add234(osize, sq2);
                }

                /*
                 * The sq we actually picked out of the tree is
                 * finished with; but its neighbours now need to
                 * appear.
                 */
                addneighbours(pick, w,h, sq->cx,sq->cy, sq->x,sq->y, matrix);
                addneighbours(cov, w,h, sq->cx,sq->cy, sq->x,sq->y, matrix);
                addneighbours(osize, w,h, sq->cx,sq->cy, sq->x,sq->y, matrix);
                sfree(sq);
            }

            /*
             * Free all remaining sq structures.
             */
            {
                struct sq *sq;
                while ((sq = delpos234(pick, 0)) != NULL)
                    sfree(sq);
            }
            freetree234(pick);
            freetree234(cov);
            freetree234(osize);

            /*
             * Finally, check to see if any two matrix rows are
             * exactly identical. If so, this is not an acceptable
             * matrix, and we give up and go round again.
             * 
             * I haven't been immediately able to think of a
             * plausible means of algorithmically avoiding this
             * situation (by, say, making a small perturbation to
             * an offending matrix), so for the moment I'm just
             * going to deal with it by throwing the whole thing
             * away. I suspect this will lead to scalability
             * problems (since most of the things happening in
             * these matrices are local, the chance of _some_
             * neighbourhood having two identical regions will
             * increase with the grid area), but so far this puzzle
             * seems to be really hard at large sizes so I'm not
             * massively worried yet. Anyone needs this done
             * better, they're welcome to submit a patch.
             */
            for (i = 0; i < wh; i++) {
                for (j = 0; j < wh; j++)
                    if (i != j &&
                        !memcmp(matrix + i * wh, matrix + j * wh, wh))
                        break;
                if (j < wh)
                    break;
            }
            if (i == wh)
                break;                 /* no matches found */
        }
        break;
    }

    /*
     * Now invent a random initial set of lights.
     * 
     * At first glance it looks as if it might be quite difficult
     * to choose equiprobably from all soluble light sets. After
     * all, soluble light sets are those in the image space of the
     * transformation matrix; so first we'd have to identify that
     * space and its dimension, then pick a random coordinate for
     * each basis vector and recombine. Lot of fiddly matrix
     * algebra there.
     * 
     * However, vector spaces are nicely orthogonal and relieve us
     * of all that difficulty. For every point in the image space,
     * there are precisely as many points in the input space that
     * map to it as there are elements in the kernel of the
     * transformation matrix (because adding any kernel element to
     * the input does not change the output, and because any two
     * inputs mapping to the same output must differ by an element
     * of the kernel because that's what the kernel _is_); and
     * these cosets are all disjoint (obviously, since no input
     * point can map to more than one output point) and cover the
     * whole space (equally obviously, because no input point can
     * map to fewer than one output point!).
     *
     * So the input space contains the same number of points for
     * each point in the output space; thus, we can simply choose
     * equiprobably from elements of the _input_ space, and filter
     * the result through the transformation matrix in the obvious
     * way, and we thereby guarantee to choose equiprobably from
     * all the output points. Phew!
     */
    while (1) {
        memset(grid, 0, wh);
        for (i = 0; i < wh; i++) {
            int v = random_upto(rs, 2);
            if (v) {
                for (j = 0; j < wh; j++)
                    grid[j] ^= matrix[i*wh+j];
            }
        }
        /*
         * Ensure we don't have the starting state already!
         */
        for (i = 0; i < wh; i++)
            if (grid[i])
                break;
        if (i < wh)
            break;
    }

    /*
     * Now encode the matrix and the starting grid as a game
     * description. We'll do this by concatenating two great big
     * hex bitmaps.
     */
    mbmp = encode_bitmap(matrix, wh*wh);
    gbmp = encode_bitmap(grid, wh);
    ret = snewn(strlen(mbmp) + strlen(gbmp) + 2, char);
    sprintf(ret, "%s,%s", mbmp, gbmp);
    sfree(mbmp);
    sfree(gbmp);
    return ret;
}

static void game_free_aux_info(game_aux_info *aux)
{
    assert(!"Shouldn't happen");
}

static char *validate_desc(game_params *params, char *desc)
{
    int w = params->w, h = params->h, wh = w * h;
    int mlen = (wh*wh+3)/4, glen = (wh+3)/4;

    if (strspn(desc, "0123456789abcdefABCDEF") != mlen)
        return "Matrix description is wrong length";
    if (desc[mlen] != ',')
        return "Expected comma after matrix description";
    if (strspn(desc+mlen+1, "0123456789abcdefABCDEF") != glen)
        return "Grid description is wrong length";
    if (desc[mlen+1+glen])
        return "Unexpected data after grid description";

    return NULL;
}

static game_state *new_game(midend_data *me, game_params *params, char *desc)
{
    int w = params->w, h = params->h, wh = w * h;
    int mlen = (wh*wh+3)/4;

    game_state *state = snew(game_state);

    state->w = w;
    state->h = h;
    state->completed = FALSE;
    state->moves = 0;
    state->matrix = snew(struct matrix);
    state->matrix->refcount = 1;
    state->matrix->matrix = snewn(wh*wh, unsigned char);
    decode_bitmap(state->matrix->matrix, wh*wh, desc);
    state->grid = snewn(wh, unsigned char);
    decode_bitmap(state->grid, wh, desc + mlen + 1);

    return state;
}

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->completed = state->completed;
    ret->moves = state->moves;
    ret->matrix = state->matrix;
    state->matrix->refcount++;
    ret->grid = snewn(ret->w * ret->h, unsigned char);
    memcpy(ret->grid, state->grid, ret->w * ret->h);

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    if (--state->matrix->refcount <= 0) {
        sfree(state->matrix->matrix);
        sfree(state->matrix);
    }
    sfree(state);
}

static game_state *solve_game(game_state *state, game_state *currstate,
			      game_aux_info *aux, char **error)
{
    return NULL;
}

static char *game_text_format(game_state *state)
{
    return NULL;
}

static game_ui *new_ui(game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
}

struct game_drawstate {
    int w, h, started;
    unsigned char *tiles;
    int tilesize;
};

static game_state *make_move(game_state *from, game_ui *ui, game_drawstate *ds,
                             int x, int y, int button)
{
    int w = from->w, h = from->h, wh = w * h;
    game_state *ret;

    if (button == LEFT_BUTTON) {
        int tx = FROMCOORD(x), ty = FROMCOORD(y);
        if (tx >= 0 && tx < w && ty >= 0 && ty < h) {
            int i, j, done;

            ret = dup_game(from);

	    if (!ret->completed)
		ret->moves++;

            i = ty * w + tx;

	    done = TRUE;
            for (j = 0; j < wh; j++) {
                ret->grid[j] ^= ret->matrix->matrix[i*wh+j];
		if (ret->grid[j] & 1)
		    done = FALSE;
	    }
	    if (done)
		ret->completed = TRUE;

            return ret;
        }
    }

    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_size(game_params *params, game_drawstate *ds,
                      int *x, int *y, int expand)
{
    int tsx, tsy, ts;
    /*
     * Each window dimension equals the tile size times one more
     * than the grid dimension (the border is half the width of the
     * tiles).
     */
    tsx = *x / (params->w + 1);
    tsy = *y / (params->h + 1);
    ts = min(tsx, tsy);
    if (expand)
        ds->tilesize = ts;
    else
        ds->tilesize = min(ts, PREFERRED_TILE_SIZE);

    *x = TILE_SIZE * params->w + 2 * BORDER;
    *y = TILE_SIZE * params->h + 2 * BORDER;
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_WRONG * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] / 3;
    ret[COL_WRONG * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] / 3;
    ret[COL_WRONG * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] / 3;

    ret[COL_RIGHT * 3 + 0] = 1.0F;
    ret[COL_RIGHT * 3 + 1] = 1.0F;
    ret[COL_RIGHT * 3 + 2] = 1.0F;

    ret[COL_GRID * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] / 1.5F;
    ret[COL_GRID * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] / 1.5F;
    ret[COL_GRID * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] / 1.5F;

    ret[COL_DIAG * 3 + 0] = ret[COL_GRID * 3 + 0];
    ret[COL_DIAG * 3 + 1] = ret[COL_GRID * 3 + 1];
    ret[COL_DIAG * 3 + 2] = ret[COL_GRID * 3 + 2];

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->started = FALSE;
    ds->w = state->w;
    ds->h = state->h;
    ds->tiles = snewn(ds->w*ds->h, unsigned char);
    ds->tilesize = 0;                  /* haven't decided yet */
    for (i = 0; i < ds->w*ds->h; i++)
        ds->tiles[i] = -1;

    return ds;
}

static void game_free_drawstate(game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds);
}

static void draw_tile(frontend *fe, game_drawstate *ds,
		      game_state *state, int x, int y, int tile, int anim,
		      float animtime)
{
    int w = ds->w, h = ds->h, wh = w * h;
    int bx = x * TILE_SIZE + BORDER, by = y * TILE_SIZE + BORDER;
    int i, j;

    clip(fe, bx+1, by+1, TILE_SIZE-1, TILE_SIZE-1);

    draw_rect(fe, bx+1, by+1, TILE_SIZE-1, TILE_SIZE-1,
              anim ? COL_BACKGROUND : tile & 1 ? COL_WRONG : COL_RIGHT);
    if (anim) {
	/*
	 * Draw a polygon indicating that the square is diagonally
	 * flipping over.
	 */
	int coords[8], colour;

	coords[0] = bx + TILE_SIZE;
	coords[1] = by;
	coords[2] = bx + TILE_SIZE * animtime;
	coords[3] = by + TILE_SIZE * animtime;
	coords[4] = bx;
	coords[5] = by + TILE_SIZE;
	coords[6] = bx + TILE_SIZE - TILE_SIZE * animtime;
	coords[7] = by + TILE_SIZE - TILE_SIZE * animtime;

	colour = (tile & 1 ? COL_WRONG : COL_RIGHT);
	if (animtime < 0.5)
	    colour = COL_WRONG + COL_RIGHT - colour;

	draw_polygon(fe, coords, 4, TRUE, colour);
	draw_polygon(fe, coords, 4, FALSE, COL_GRID);
    }

    /*
     * Draw a little diagram in the tile which indicates which
     * surrounding tiles flip when this one is clicked.
     */
    for (i = 0; i < h; i++)
	for (j = 0; j < w; j++)
	    if (state->matrix->matrix[(y*w+x)*wh + i*w+j]) {
		int ox = j - x, oy = i - y;
		int td = TILE_SIZE / 16;
		int cx = (bx + TILE_SIZE/2) + (2 * ox - 1) * td;
		int cy = (by + TILE_SIZE/2) + (2 * oy - 1) * td;
		if (ox == 0 && oy == 0)
                    draw_rect(fe, cx, cy, 2*td+1, 2*td+1, COL_DIAG);
                else {
                    draw_line(fe, cx, cy, cx+2*td, cy, COL_DIAG);
                    draw_line(fe, cx, cy+2*td, cx+2*td, cy+2*td, COL_DIAG);
                    draw_line(fe, cx, cy, cx, cy+2*td, COL_DIAG);
                    draw_line(fe, cx+2*td, cy, cx+2*td, cy+2*td, COL_DIAG);
                }
	    }

    unclip(fe);

    draw_update(fe, bx+1, by+1, TILE_SIZE-1, TILE_SIZE-1);
}

static void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int w = ds->w, h = ds->h, wh = w * h;
    int i, flashframe;

    if (!ds->started) {
        draw_rect(fe, 0, 0, TILE_SIZE * w + 2 * BORDER,
                  TILE_SIZE * h + 2 * BORDER, COL_BACKGROUND);

        /*
         * Draw the grid lines.
         */
        for (i = 0; i <= w; i++)
            draw_line(fe, i * TILE_SIZE + BORDER, BORDER,
                      i * TILE_SIZE + BORDER, h * TILE_SIZE + BORDER,
                      COL_GRID);
        for (i = 0; i <= h; i++)
            draw_line(fe, BORDER, i * TILE_SIZE + BORDER,
                      w * TILE_SIZE + BORDER, i * TILE_SIZE + BORDER,
                      COL_GRID);

        draw_update(fe, 0, 0, TILE_SIZE * w + 2 * BORDER,
                    TILE_SIZE * h + 2 * BORDER);

        ds->started = TRUE;
    }

    if (flashtime)
	flashframe = flashtime / FLASH_FRAME;
    else
	flashframe = -1;

    animtime /= ANIM_TIME;	       /* scale it so it goes from 0 to 1 */

    for (i = 0; i < wh; i++) {
        int x = i % w, y = i / w;
	int fx, fy, fd;
	int v = state->grid[i];
	int vv;

	if (flashframe >= 0) {
	    fx = (w+1)/2 - min(x+1, w-x);
	    fy = (h+1)/2 - min(y+1, h-y);
	    fd = max(fx, fy);
	    if (fd == flashframe)
		v |= 1;
	    else if (fd == flashframe - 1)
		v &= ~1;
	}

	if (oldstate && state->grid[i] != oldstate->grid[i])
	    vv = 255;		       /* means `animated' */
	else
	    vv = v;

        if (ds->tiles[i] == 255 || vv == 255 || ds->tiles[i] != vv) {
            draw_tile(fe, ds, state, x, y, v, vv == 255, animtime);
            ds->tiles[i] = vv;
        }
    }

    {
	char buf[256];

	sprintf(buf, "%sMoves: %d", state->completed ? "COMPLETED! " : "",
		state->moves);

	status_bar(fe, buf);
    }
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
			      int dir, game_ui *ui)
{
    return ANIM_TIME;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
			       int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed)
        return FLASH_FRAME * (max((newstate->w+1)/2, (newstate->h+1)/2)+1);

    return 0.0F;
}

static int game_wants_statusbar(void)
{
    return TRUE;
}

static int game_timing_state(game_state *state)
{
    return TRUE;
}

#ifdef COMBINED
#define thegame flip
#endif

const struct game thegame = {
    "Flip", NULL,
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    game_free_aux_info,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    FALSE, solve_game,
    FALSE, game_text_format,
    new_ui,
    free_ui,
    game_changed_state,
    make_move,
    game_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,				       /* mouse_priorities */
};
