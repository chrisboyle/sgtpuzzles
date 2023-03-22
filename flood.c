/*
 * flood.c: puzzle in which you make a grid all the same colour by
 * repeatedly flood-filling the top left corner, and try to do so in
 * as few moves as possible.
 */

/*
 * Possible further work:
 *
 *  - UI: perhaps we should only permit clicking on regions that can
 *    actually be reached by the next flood-fill - i.e. a click is
 *    only interpreted as a move if it would cause the clicked-on
 *    square to become part of the controlled area. This provides a
 *    hint in cases where you mistakenly thought that would happen,
 *    and protects you against typos in cases where you just
 *    mis-aimed.
 *
 *  - UI: perhaps mark the fill square in some way? Or even mark the
 *    whole connected component _containing_ the fill square. Pro:
 *    that would make it easier to tell apart cases where almost all
 *    the yellow squares in the grid are part of the target component
 *    (hence, yellow is _done_ and you never have to fill in that
 *    colour again) from cases where there's still one yellow square
 *    that's only diagonally adjacent and hence will need coming back
 *    to. Con: but it would almost certainly be ugly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"

enum {
    COL_BACKGROUND, COL_SEPARATOR,
    COL_1, COL_2, COL_3, COL_4, COL_5, COL_6, COL_7, COL_8, COL_9, COL_10,
    COL_HIGHLIGHT, COL_LOWLIGHT,
    NCOLOURS
};

struct game_params {
    int w, h;
    int colours;
    int leniency;
};

/* Just in case I want to make this changeable later, I'll put the
 * coordinates of the flood-fill point here so that it'll be easy to
 * find everywhere later that has to change. */
#define FILLX 0
#define FILLY 0

typedef struct soln {
    int refcount;
    int nmoves;
    char *moves;
} soln;

struct game_state {
    int w, h, colours;
    int moves, movelimit;
    bool complete;
    char *grid;
    bool cheated;
    int solnpos;
    soln *soln;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = ret->h = 12;
    ret->colours = 6;
    ret->leniency = 5;

    return ret;
}

static const struct {
    struct game_params preset;
    const char *name;
} flood_presets[] = {
    /* Default 12x12 size, three difficulty levels. */
    {{12, 12, 6, 5}, "12x12 Easy"},
    {{12, 12, 6, 2}, "12x12 Medium"},
    {{12, 12, 6, 0}, "12x12 Hard"},
    /* Larger puzzles, leaving off Easy in the expectation that people
     * wanting a bigger grid will have played it enough to find Easy
     * easy. */
    {{16, 16, 6, 2}, "16x16 Medium"},
    {{16, 16, 6, 0}, "16x16 Hard"},
    /* A couple of different colour counts. It seems generally not too
     * hard with fewer colours (probably because fewer choices), so no
     * extra moves for these modes. */
    {{12, 12, 3, 0}, "12x12, 3 colours"},
    {{12, 12, 4, 0}, "12x12, 4 colours"},
};

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;

    if (i < 0 || i >= lenof(flood_presets))
        return false;

    ret = snew(game_params);
    *ret = flood_presets[i].preset;
    *name = dupstr(flood_presets[i].name);
    *params = ret;
    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    ret->w = ret->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    while (*string) {
        if (*string == 'c') {
            string++;
	    ret->colours = atoi(string);
            while (*string && isdigit((unsigned char)*string)) string++;
	} else if (*string == 'm') {
            string++;
	    ret->leniency = atoi(string);
            while (*string && isdigit((unsigned char)*string)) string++;
	} else
            string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[256];
    sprintf(buf, "%dx%d", params->w, params->h);
    if (full)
        sprintf(buf + strlen(buf), "c%dm%d",
                params->colours, params->leniency);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Colours";
    ret[2].type = C_STRING;
    sprintf(buf, "%d", params->colours);
    ret[2].u.string.sval = dupstr(buf);

    ret[3].name = "Extra moves permitted";
    ret[3].type = C_STRING;
    sprintf(buf, "%d", params->leniency);
    ret[3].u.string.sval = dupstr(buf);

    ret[4].name = NULL;
    ret[4].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->colours = atoi(cfg[2].u.string.sval);
    ret->leniency = atoi(cfg[3].u.string.sval);

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 2 && params->h < 2)
        return "Grid must contain at least two squares";
    if (params->w < 1 || params->h < 1)
        return "Width and height must be at least one";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";
    if (params->colours < 3 || params->colours > 10)
        return "Must have between 3 and 10 colours";
    if (params->leniency < 0)
        return "Leniency must be non-negative";
    return NULL;
}

#if 0
/*
 * Bodge to permit varying the recursion depth for testing purposes.

To test two Floods against each other:

paste <(./flood.1 --generate 100 12x12c6m0#12345 | cut -f2 -d,) <(./flood.2 --generate 100 12x12c6m0#12345 | cut -f2 -d,) | awk '{print $2-$1}' | sort -n | uniq -c | awk '{print $2,$1}' | tee z

and then run gnuplot and plot "z".

 */
static int rdepth = 0;
#define RECURSION_DEPTH (rdepth)
void check_recursion_depth(void)
{
    if (!rdepth) {
        const char *depthstr = getenv("FLOOD_DEPTH");
        rdepth = depthstr ? atoi(depthstr) : 1;
        rdepth = rdepth > 0 ? rdepth : 1;
    }
}
#else
/*
 * Last time I empirically checked this, depth 3 was a noticeable
 * improvement on 2, but 4 only negligibly better than 3.
 */
#define RECURSION_DEPTH 3
#define check_recursion_depth() (void)0
#endif

struct solver_scratch {
    int *queue[2];
    int *dist;
    char *grid, *grid2;
    char *rgrids;
};

static struct solver_scratch *new_scratch(int w, int h)
{
    int wh = w*h;
    struct solver_scratch *scratch = snew(struct solver_scratch);
    check_recursion_depth();
    scratch->queue[0] = snewn(wh, int);
    scratch->queue[1] = snewn(wh, int);
    scratch->dist = snewn(wh, int);
    scratch->grid = snewn(wh, char);
    scratch->grid2 = snewn(wh, char);
    scratch->rgrids = snewn(wh * RECURSION_DEPTH, char);
    return scratch;
}

static void free_scratch(struct solver_scratch *scratch)
{
    sfree(scratch->queue[0]);
    sfree(scratch->queue[1]);
    sfree(scratch->dist);
    sfree(scratch->grid);
    sfree(scratch->grid2);
    sfree(scratch->rgrids);
    sfree(scratch);
}

#if 0
/* Diagnostic routines you can uncomment if you need them */
void dump_grid(int w, int h, const char *grid, const char *titlefmt, ...)
{
    int x, y;
    if (titlefmt) {
        va_list ap;
        va_start(ap, titlefmt);
        vprintf(titlefmt, ap);
        va_end(ap);
        printf(":\n");
    } else {
        printf("Grid:\n");
    }
    for (y = 0; y < h; y++) {
        printf("  ");
        for (x = 0; x < w; x++) {
            printf("%1x", grid[y*w+x]);
        }
        printf("\n");
    }
}

void dump_dist(int w, int h, const int *dists, const char *titlefmt, ...)
{
    int x, y;
    if (titlefmt) {
        va_list ap;
        va_start(ap, titlefmt);
        vprintf(titlefmt, ap);
        va_end(ap);
        printf(":\n");
    } else {
        printf("Distances:\n");
    }
    for (y = 0; y < h; y++) {
        printf("  ");
        for (x = 0; x < w; x++) {
            printf("%3d", dists[y*w+x]);
        }
        printf("\n");
    }
}
#endif

/*
 * Search a grid to find the most distant square(s). Return their
 * distance and the number of them, and also the number of squares in
 * the current controlled set (i.e. at distance zero).
 */
static void search(int w, int h, char *grid, int x0, int y0,
                   struct solver_scratch *scratch,
                   int *rdist, int *rnumber, int *rcontrol)
{
    int wh = w*h;
    int i, qcurr, qhead, qtail, qnext, currdist, remaining;

    for (i = 0; i < wh; i++)
        scratch->dist[i] = -1;
    scratch->queue[0][0] = y0*w+x0;
    scratch->queue[1][0] = y0*w+x0;
    scratch->dist[y0*w+x0] = 0;
    currdist = 0;
    qcurr = 0;
    qtail = 0;
    qhead = 1;
    qnext = 1;
    remaining = wh - 1;

    while (1) {
        if (qtail == qhead) {
            /* Switch queues. */
            if (currdist == 0)
                *rcontrol = qhead;
            currdist++;
            qcurr ^= 1;                    /* switch queues */
            qhead = qnext;
            qtail = 0;
            qnext = 0;
#if 0
            printf("switch queue, new dist %d, queue %d\n", currdist, qhead);
#endif
        } else if (remaining == 0 && qnext == 0) {
            break;
        } else {
            int pos = scratch->queue[qcurr][qtail++];
            int y = pos / w;
            int x = pos % w;
#if 0
            printf("checking neighbours of %d,%d\n", x, y);
#endif
            int dir;
            for (dir = 0; dir < 4; dir++) {
                int y1 = y + (dir == 1 ? 1 : dir == 3 ? -1 : 0);
                int x1 = x + (dir == 0 ? 1 : dir == 2 ? -1 : 0);
                if (0 <= x1 && x1 < w && 0 <= y1 && y1 < h) {
                    int pos1 = y1*w+x1;
#if 0
                    printf("trying %d,%d: colours %d-%d dist %d\n", x1, y1,
                           grid[pos], grid[pos1], scratch->dist[pos]);
#endif
                    if (scratch->dist[pos1] == -1 &&
                        ((grid[pos1] == grid[pos] &&
                          scratch->dist[pos] == currdist) ||
                         (grid[pos1] != grid[pos] &&
                          scratch->dist[pos] == currdist - 1))) {
#if 0
                        printf("marking %d,%d dist %d\n", x1, y1, currdist);
#endif
                        scratch->queue[qcurr][qhead++] = pos1;
                        scratch->queue[qcurr^1][qnext++] = pos1;
                        scratch->dist[pos1] = currdist;
                        remaining--;
                    }
                }
            }
        }
    }

    *rdist = currdist;
    *rnumber = qhead;
    if (currdist == 0)
        *rcontrol = qhead;
}

/*
 * Enact a flood-fill move on a grid.
 */
static void fill(int w, int h, char *grid, int x0, int y0, char newcolour,
                 int *queue)
{
    char oldcolour;
    int qhead, qtail;

    oldcolour = grid[y0*w+x0];
    assert(oldcolour != newcolour);
    grid[y0*w+x0] = newcolour;
    queue[0] = y0*w+x0;
    qtail = 0;
    qhead = 1;

    while (qtail < qhead) {
        int pos = queue[qtail++];
        int y = pos / w;
        int x = pos % w;
        int dir;
        for (dir = 0; dir < 4; dir++) {
            int y1 = y + (dir == 1 ? 1 : dir == 3 ? -1 : 0);
            int x1 = x + (dir == 0 ? 1 : dir == 2 ? -1 : 0);
            if (0 <= x1 && x1 < w && 0 <= y1 && y1 < h) {
                int pos1 = y1*w+x1;
                if (grid[pos1] == oldcolour) {
                    grid[pos1] = newcolour;
                    queue[qhead++] = pos1;
                }
            }
        }
    }
}

/*
 * Detect a completed grid.
 */
static bool completed(int w, int h, char *grid)
{
    int wh = w*h;
    int i;

    for (i = 1; i < wh; i++)
        if (grid[i] != grid[0])
            return false;

    return true;
}

/*
 * Try out every possible move on a grid, and choose whichever one
 * reduced the result of search() by the most.
 */
static char choosemove_recurse(int w, int h, char *grid, int x0, int y0,
                               int maxmove, struct solver_scratch *scratch,
                               int depth, int *rbestdist, int *rbestnumber, int *rbestcontrol)
{
    int wh = w*h;
    char move, bestmove;
    int dist, number, control, bestdist, bestnumber, bestcontrol;
    char *tmpgrid;

    assert(0 <= depth && depth < RECURSION_DEPTH);
    tmpgrid = scratch->rgrids + depth*wh;

    bestdist = wh + 1;
    bestnumber = 0;
    bestcontrol = 0;
    bestmove = -1;

#if 0
    dump_grid(w, h, grid, "before choosemove_recurse %d", depth);
#endif
    for (move = 0; move < maxmove; move++) {
        if (grid[y0*w+x0] == move)
            continue;
        memcpy(tmpgrid, grid, wh * sizeof(*grid));
        fill(w, h, tmpgrid, x0, y0, move, scratch->queue[0]);
        if (completed(w, h, tmpgrid)) {
            /*
             * A move that wins is immediately the best, so stop
             * searching. Record what depth of recursion that happened
             * at, so that higher levels will choose a move that gets
             * to a winning position sooner.
             */
            *rbestdist = -1;
            *rbestnumber = depth;
            *rbestcontrol = wh;
            return move;
        }
        if (depth < RECURSION_DEPTH-1) {
            choosemove_recurse(w, h, tmpgrid, x0, y0, maxmove, scratch,
                               depth+1, &dist, &number, &control);
        } else {
#if 0
            dump_grid(w, h, tmpgrid, "after move %d at depth %d",
                      move, depth);
#endif
            search(w, h, tmpgrid, x0, y0, scratch, &dist, &number, &control);
#if 0
            dump_dist(w, h, scratch->dist, "after move %d at depth %d",
                      move, depth);
            printf("move %d at depth %d: %d at %d\n",
                   depth, move, number, dist);
#endif
        }
        if (dist < bestdist ||
            (dist == bestdist &&
             (number < bestnumber ||
              (number == bestnumber &&
               (control > bestcontrol))))) {
            bestdist = dist;
            bestnumber = number;
            bestcontrol = control;
            bestmove = move;
        }
    }
#if 0
    printf("best at depth %d was %d (%d at %d, %d controlled)\n",
           depth, bestmove, bestnumber, bestdist, bestcontrol);
#endif

    *rbestdist = bestdist;
    *rbestnumber = bestnumber;
    *rbestcontrol = bestcontrol;
    return bestmove;
}
static char choosemove(int w, int h, char *grid, int x0, int y0,
                       int maxmove, struct solver_scratch *scratch)
{
    int tmp0, tmp1, tmp2;
    return choosemove_recurse(w, h, grid, x0, y0, maxmove, scratch,
                              0, &tmp0, &tmp1, &tmp2);
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    int w = params->w, h = params->h, wh = w*h;
    int i, moves;
    char *desc;
    struct solver_scratch *scratch;

    scratch = new_scratch(w, h);

    /*
     * Invent a random grid.
     */
    do {
        for (i = 0; i < wh; i++)
            scratch->grid[i] = random_upto(rs, params->colours);
    } while (completed(w, h, scratch->grid));

    /*
     * Run the solver, and count how many moves it uses.
     */
    memcpy(scratch->grid2, scratch->grid, wh * sizeof(*scratch->grid2));
    moves = 0;
    check_recursion_depth();
    while (!completed(w, h, scratch->grid2)) {
        char move = choosemove(w, h, scratch->grid2, FILLX, FILLY,
                               params->colours, scratch);
        fill(w, h, scratch->grid2, FILLX, FILLY, move, scratch->queue[0]);
        moves++;
    }

    /*
     * Adjust for difficulty.
     */
    moves += params->leniency;

    /*
     * Encode the game id.
     */
    desc = snewn(wh + 40, char);
    for (i = 0; i < wh; i++) {
        char colour = scratch->grid[i];
        char textcolour = (colour > 9 ? 'A' : '0') + colour;
        desc[i] = textcolour;
    }
    sprintf(desc+i, ",%d", moves);

    free_scratch(scratch);

    return desc;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int w = params->w, h = params->h, wh = w*h;
    int i;
    for (i = 0; i < wh; i++) {
        char c = *desc++;
        if (c == 0)
            return "Not enough data in grid description";
        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'A' && c <= 'Z')
            c = 10 + (c - 'A');
        else
            return "Bad character in grid description";
        if ((unsigned)c >= params->colours)
            return "Colour out of range in grid description";
    }
    if (*desc != ',')
        return "Expected ',' after grid description";
    desc++;
    if (desc[strspn(desc, "0123456789")])
        return "Badly formatted move limit after grid description";
    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int w = params->w, h = params->h, wh = w*h;
    game_state *state = snew(game_state);
    int i;

    state->w = w;
    state->h = h;
    state->colours = params->colours;
    state->moves = 0;
    state->grid = snewn(wh, char);

    for (i = 0; i < wh; i++) {
        char c = *desc++;
        assert(c);
        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'A' && c <= 'Z')
            c = 10 + (c - 'A');
        else
            assert(!"bad colour");
        state->grid[i] = c;
    }
    assert(*desc == ',');
    desc++;

    state->movelimit = atoi(desc);
    state->complete = false;
    state->cheated = false;
    state->solnpos = 0;
    state->soln = NULL;

    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->colours = state->colours;
    ret->moves = state->moves;
    ret->movelimit = state->movelimit;
    ret->complete = state->complete;
    ret->grid = snewn(state->w * state->h, char);
    memcpy(ret->grid, state->grid, state->w * state->h * sizeof(*ret->grid));

    ret->cheated = state->cheated;
    ret->soln = state->soln;
    if (ret->soln)
	ret->soln->refcount++;
    ret->solnpos = state->solnpos;

    return ret;
}

static void free_game(game_state *state)
{
    if (state->soln && --state->soln->refcount == 0) {
	sfree(state->soln->moves);
	sfree(state->soln);
    }
    sfree(state->grid);
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    int w = state->w, h = state->h, wh = w*h;
    char *moves, *ret, *p;
    int i, len, nmoves;
    char buf[256];
    struct solver_scratch *scratch;

    if (currstate->complete) {
        *error = "Puzzle is already solved";
        return NULL;
    }

    /*
     * Find the best solution our solver can give.
     */
    moves = snewn(wh, char);           /* sure to be enough */
    nmoves = 0;
    scratch = new_scratch(w, h);
    memcpy(scratch->grid2, currstate->grid, wh * sizeof(*scratch->grid2));
    check_recursion_depth();
    while (!completed(w, h, scratch->grid2)) {
        char move = choosemove(w, h, scratch->grid2, FILLX, FILLY,
                               currstate->colours, scratch);
        fill(w, h, scratch->grid2, FILLX, FILLY, move, scratch->queue[0]);
        assert(nmoves < wh);
        moves[nmoves++] = move;
    }
    free_scratch(scratch);

    /*
     * Encode it as a move string.
     */
    len = 1;                           /* trailing NUL */
    for (i = 0; i < nmoves; i++)
        len += sprintf(buf, ",%d", moves[i]);
    ret = snewn(len, char);
    p = ret;
    for (i = 0; i < nmoves; i++)
        p += sprintf(p, "%c%d", (i==0 ? 'S' : ','), moves[i]);
    assert(p - ret == len - 1);

    sfree(moves);
    return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w = state->w, h = state->h;
    char *ret, *p;
    int x, y, len;

    len = h * (w+1);                   /* +1 for newline after each row */
    ret = snewn(len+1, char);          /* and +1 for terminating \0 */
    p = ret;

    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++) {
            char colour = state->grid[y*w+x];
            char textcolour = (colour > 9 ? 'A' : '0') + colour;
            *p++ = textcolour;
	}
	*p++ = '\n';
    }

    assert(p - ret == len);
    *p = '\0';

    return ret;
}

struct game_ui {
    bool cursor_visible;
    int cx, cy;
    enum { VICTORY, DEFEAT } flash_type;
};

static game_ui *new_ui(const game_state *state)
{
    struct game_ui *ui = snew(struct game_ui);
    ui->cursor_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->cx = FILLX;
    ui->cy = FILLY;
    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(const game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    if (button == CURSOR_SELECT &&
        state->grid[0] != state->grid[ui->cy*state->w+ui->cx])
        return "Fill";
    if (button == CURSOR_SELECT2 &&
        state->soln && state->solnpos < state->soln->nmoves)
        return "Advance";
    return "";
}

struct game_drawstate {
    bool started;
    int tilesize;
    int *grid;
};

#define TILESIZE (ds->tilesize)
#define PREFERRED_TILESIZE 32
#define BORDER (TILESIZE / 2)
#define SEP_WIDTH (TILESIZE / 32)
#define CURSOR_INSET (TILESIZE / 8)
#define HIGHLIGHT_WIDTH (TILESIZE / 10)
#define COORD(x)  ( (x) * TILESIZE + BORDER )
#define FROMCOORD(x)  ( ((x) - BORDER + TILESIZE) / TILESIZE - 1 )
#define VICTORY_FLASH_FRAME 0.03F
#define DEFEAT_FLASH_FRAME 0.10F

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int w = state->w, h = state->h;
    int tx = -1, ty = -1, move = -1;

    if (button == LEFT_BUTTON) {
	tx = FROMCOORD(x);
        ty = FROMCOORD(y);
        ui->cursor_visible = false;
    } else if (button == CURSOR_LEFT && ui->cx > 0) {
        ui->cx--;
        ui->cursor_visible = true;
        return UI_UPDATE;
    } else if (button == CURSOR_RIGHT && ui->cx+1 < w) {
        ui->cx++;
        ui->cursor_visible = true;
        return UI_UPDATE;
    } else if (button == CURSOR_UP && ui->cy > 0) {
        ui->cy--;
        ui->cursor_visible = true;
        return UI_UPDATE;
    } else if (button == CURSOR_DOWN && ui->cy+1 < h) {
        ui->cy++;
        ui->cursor_visible = true;
        return UI_UPDATE;
    } else if (button == CURSOR_SELECT) {
        tx = ui->cx;
        ty = ui->cy;
    } else if (button == CURSOR_SELECT2 &&
               state->soln && state->solnpos < state->soln->nmoves) {
	move = state->soln->moves[state->solnpos];
    } else {
        return NULL;
    }

    if (tx >= 0 && tx < w && ty >= 0 && ty < h &&
        state->grid[0] != state->grid[ty*w+tx])
        move = state->grid[ty*w+tx];

    if (move >= 0 && !state->complete) {
        char buf[256];
        sprintf(buf, "M%d", move);
        return dupstr(buf);
    }

    return NULL;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret;
    int c;

    if (move[0] == 'M' &&
        sscanf(move+1, "%d", &c) == 1 &&
        c >= 0 && c < state->colours &&
        c != state->grid[FILLY * state->w + FILLX] &&
        !state->complete) {
        int *queue = snewn(state->w * state->h, int);
	ret = dup_game(state);
        fill(ret->w, ret->h, ret->grid, FILLX, FILLY, c, queue);
        ret->moves++;
        ret->complete = completed(ret->w, ret->h, ret->grid);

        if (ret->soln) {
            /*
             * If this move is the correct next one in the stored
             * solution path, advance solnpos.
             */
            if (c == ret->soln->moves[ret->solnpos] &&
                ret->solnpos+1 < ret->soln->nmoves) {
                ret->solnpos++;
            } else {
                /*
                 * Otherwise, the user has strayed from the path or
                 * else the path has come to an end; either way, the
                 * path is no longer valid.
                 */
                ret->soln->refcount--;
                assert(ret->soln->refcount > 0);/* `state' at least still exists */
                ret->soln = NULL;
                ret->solnpos = 0;
            }
        }

        sfree(queue);
        return ret;
    } else if (*move == 'S') {
	soln *sol;
        const char *p;
        int i;

	/*
	 * This is a solve move, so we don't actually _change_ the
	 * grid but merely set up a stored solution path.
	 */
	move++;
	sol = snew(soln);

        sol->nmoves = 1;
        for (p = move; *p; p++) {
            if (*p == ',')
                sol->nmoves++;
        }

        sol->moves = snewn(sol->nmoves, char);
        for (i = 0, p = move; i < sol->nmoves; i++) {
            if (!*p) {
              badsolve:
                sfree(sol->moves);
                sfree(sol);
                return NULL;
            };
            sol->moves[i] = atoi(p);
            if (sol->moves[i] < 0 || sol->moves[i] >= state->colours ||
                (i == 0 ?
                 sol->moves[i] == state->grid[FILLY * state->w + FILLX] :
                 sol->moves[i] == sol->moves[i-1]))
                /* Solution contains a fill with an invalid colour or
                 * the current colour. */
                goto badsolve;
            p += strspn(p, "0123456789");
            if (*p) {
                if (*p != ',') goto badsolve;
                p++;
            }
        }

	ret = dup_game(state);
	ret->cheated = true;
	if (ret->soln && --ret->soln->refcount == 0) {
	    sfree(ret->soln->moves);
	    sfree(ret->soln);
	}
	ret->soln = sol;
	ret->solnpos = 0;
	sol->refcount = 1;
	return ret;
    }

    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = BORDER * 2 + TILESIZE * params->w;
    *y = BORDER * 2 + TILESIZE * params->h;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    ret[COL_SEPARATOR * 3 + 0] = 0.0F;
    ret[COL_SEPARATOR * 3 + 1] = 0.0F;
    ret[COL_SEPARATOR * 3 + 2] = 0.0F;

    /* red */
    ret[COL_1 * 3 + 0] = 1.0F;
    ret[COL_1 * 3 + 1] = 0.0F;
    ret[COL_1 * 3 + 2] = 0.0F;

    /* yellow */
    ret[COL_2 * 3 + 0] = 1.0F;
    ret[COL_2 * 3 + 1] = 1.0F;
    ret[COL_2 * 3 + 2] = 0.0F;

    /* green */
    ret[COL_3 * 3 + 0] = 0.0F;
    ret[COL_3 * 3 + 1] = 1.0F;
    ret[COL_3 * 3 + 2] = 0.0F;

    /* blue */
    ret[COL_4 * 3 + 0] = 0.2F;
    ret[COL_4 * 3 + 1] = 0.3F;
    ret[COL_4 * 3 + 2] = 1.0F;

    /* orange */
    ret[COL_5 * 3 + 0] = 1.0F;
    ret[COL_5 * 3 + 1] = 0.5F;
    ret[COL_5 * 3 + 2] = 0.0F;

    /* purple */
    ret[COL_6 * 3 + 0] = 0.5F;
    ret[COL_6 * 3 + 1] = 0.0F;
    ret[COL_6 * 3 + 2] = 0.7F;

    /* brown */
    ret[COL_7 * 3 + 0] = 0.5F;
    ret[COL_7 * 3 + 1] = 0.3F;
    ret[COL_7 * 3 + 2] = 0.3F;

    /* light blue */
    ret[COL_8 * 3 + 0] = 0.4F;
    ret[COL_8 * 3 + 1] = 0.8F;
    ret[COL_8 * 3 + 2] = 1.0F;

    /* light green */
    ret[COL_9 * 3 + 0] = 0.7F;
    ret[COL_9 * 3 + 1] = 1.0F;
    ret[COL_9 * 3 + 2] = 0.7F;

    /* pink */
    ret[COL_10 * 3 + 0] = 1.0F;
    ret[COL_10 * 3 + 1] = 0.6F;
    ret[COL_10 * 3 + 2] = 1.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int w = state->w, h = state->h, wh = w*h;
    int i;

    ds->started = false;
    ds->tilesize = 0;
    ds->grid = snewn(wh, int);
    for (i = 0; i < wh; i++)
        ds->grid[i] = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

#define BORDER_L  0x001
#define BORDER_R  0x002
#define BORDER_U  0x004
#define BORDER_D  0x008
#define CORNER_UL 0x010
#define CORNER_UR 0x020
#define CORNER_DL 0x040
#define CORNER_DR 0x080
#define CURSOR    0x100
#define BADFLASH  0x200
#define SOLNNEXT  0x400
#define COLOUR_SHIFT 11

static void draw_tile(drawing *dr, game_drawstate *ds,
                      int x, int y, int tile)
{
    int colour;
    int tx = COORD(x), ty = COORD(y);

    colour = tile >> COLOUR_SHIFT;
    if (tile & BADFLASH)
        colour = COL_SEPARATOR;
    else
        colour += COL_1;
    draw_rect(dr, tx, ty, TILESIZE, TILESIZE, colour);

    if (tile & BORDER_L)
        draw_rect(dr, tx, ty,
                  SEP_WIDTH, TILESIZE, COL_SEPARATOR);
    if (tile & BORDER_R)
        draw_rect(dr, tx + TILESIZE - SEP_WIDTH, ty,
                  SEP_WIDTH, TILESIZE, COL_SEPARATOR);
    if (tile & BORDER_U)
        draw_rect(dr, tx, ty,
                  TILESIZE, SEP_WIDTH, COL_SEPARATOR);
    if (tile & BORDER_D)
        draw_rect(dr, tx, ty + TILESIZE - SEP_WIDTH,
                  TILESIZE, SEP_WIDTH, COL_SEPARATOR);

    if (tile & CORNER_UL)
        draw_rect(dr, tx, ty,
                  SEP_WIDTH, SEP_WIDTH, COL_SEPARATOR);
    if (tile & CORNER_UR)
        draw_rect(dr, tx + TILESIZE - SEP_WIDTH, ty,
                  SEP_WIDTH, SEP_WIDTH, COL_SEPARATOR);
    if (tile & CORNER_DL)
        draw_rect(dr, tx, ty + TILESIZE - SEP_WIDTH,
                  SEP_WIDTH, SEP_WIDTH, COL_SEPARATOR);
    if (tile & CORNER_DR)
        draw_rect(dr, tx + TILESIZE - SEP_WIDTH, ty + TILESIZE - SEP_WIDTH,
                  SEP_WIDTH, SEP_WIDTH, COL_SEPARATOR);

    if (tile & CURSOR)
        draw_rect_outline(dr, tx + CURSOR_INSET, ty + CURSOR_INSET,
                          TILESIZE - 1 - CURSOR_INSET * 2,
                          TILESIZE - 1 - CURSOR_INSET * 2,
                          COL_SEPARATOR);

    if (tile & SOLNNEXT) {
        draw_circle(dr, tx + TILESIZE/2, ty + TILESIZE/2, TILESIZE/6,
                    COL_SEPARATOR, COL_SEPARATOR);
    }

    draw_update(dr, tx, ty, TILESIZE, TILESIZE);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int w = state->w, h = state->h, wh = w*h;
    int x, y, flashframe, solnmove;
    char *grid;

    /* This was entirely cloned from fifteen.c; it should probably be
     * moved into some generic 'draw-recessed-rectangle' utility fn. */
    if (!ds->started) {
	int coords[10];

	/*
	 * Recessed area containing the whole puzzle.
	 */
	coords[0] = COORD(w) + HIGHLIGHT_WIDTH - 1;
	coords[1] = COORD(h) + HIGHLIGHT_WIDTH - 1;
	coords[2] = COORD(w) + HIGHLIGHT_WIDTH - 1;
	coords[3] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[4] = coords[2] - TILESIZE;
	coords[5] = coords[3] + TILESIZE;
	coords[8] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[9] = COORD(h) + HIGHLIGHT_WIDTH - 1;
	coords[6] = coords[8] + TILESIZE;
	coords[7] = coords[9] - TILESIZE;
	draw_polygon(dr, coords, 5, COL_HIGHLIGHT, COL_HIGHLIGHT);

	coords[1] = COORD(0) - HIGHLIGHT_WIDTH;
	coords[0] = COORD(0) - HIGHLIGHT_WIDTH;
	draw_polygon(dr, coords, 5, COL_LOWLIGHT, COL_LOWLIGHT);

        draw_rect(dr, COORD(0) - SEP_WIDTH, COORD(0) - SEP_WIDTH,
                  TILESIZE * w + 2 * SEP_WIDTH, TILESIZE * h + 2 * SEP_WIDTH,
                  COL_SEPARATOR);

	ds->started = true;
    }

    if (flashtime > 0) {
        float frame = (ui->flash_type == VICTORY ?
                       VICTORY_FLASH_FRAME : DEFEAT_FLASH_FRAME);
        flashframe = (int)(flashtime / frame);
    } else {
        flashframe = -1;
    }

    grid = snewn(wh, char);
    memcpy(grid, state->grid, wh * sizeof(*grid));

    if (state->soln && state->solnpos < state->soln->nmoves) {
        int i, *queue;

        /*
         * Highlight as 'next auto-solver move' every square of the
         * target colour which is adjacent to the currently controlled
         * region. We do this by first enacting the actual move, then
         * flood-filling again in a nonexistent colour, and finally
         * reverting to the original grid anything in the new colour
         * that was part of the original controlled region. Then
         * regions coloured in the dummy colour should be displayed as
         * soln_move with the SOLNNEXT flag.
         */
        solnmove = state->soln->moves[state->solnpos];

        queue = snewn(wh, int);
        fill(w, h, grid, FILLX, FILLY, solnmove, queue);
        fill(w, h, grid, FILLX, FILLY, state->colours, queue);
        sfree(queue);

        for (i = 0; i < wh; i++)
            if (grid[i] == state->colours && state->grid[i] != solnmove)
                grid[i] = state->grid[i];
    } else {
        solnmove = 0;                  /* placate optimiser */
    }

    if (flashframe >= 0 && ui->flash_type == VICTORY) {
        /*
         * Modify the display grid by superimposing our rainbow flash
         * on it.
         */
        for (x = 0; x < w; x++) {
            for (y = 0; y < h; y++) {
                int flashpos = flashframe - (abs(x - FILLX) + abs(y - FILLY));
                if (flashpos >= 0 && flashpos < state->colours)
                    grid[y*w+x] = flashpos;
            }
        }
    }

    for (x = 0; x < w; x++) {
	for (y = 0; y < h; y++) {
            int pos = y*w+x;
            int tile;

            if (grid[pos] == state->colours) {
                tile = (solnmove << COLOUR_SHIFT) | SOLNNEXT;
            } else {
                tile = (int)grid[pos] << COLOUR_SHIFT;
            }

            if (x == 0 || grid[pos-1] != grid[pos])
                tile |= BORDER_L;
            if (x==w-1 || grid[pos+1] != grid[pos])
                tile |= BORDER_R;
            if (y == 0 || grid[pos-w] != grid[pos])
                tile |= BORDER_U;
            if (y==h-1 || grid[pos+w] != grid[pos])
                tile |= BORDER_D;
            if (x == 0 || y == 0 || grid[pos-w-1] != grid[pos])
                tile |= CORNER_UL;
            if (x==w-1 || y == 0 || grid[pos-w+1] != grid[pos])
                tile |= CORNER_UR;
            if (x == 0 || y==h-1 || grid[pos+w-1] != grid[pos])
                tile |= CORNER_DL;
            if (x==w-1 || y==h-1 || grid[pos+w+1] != grid[pos])
                tile |= CORNER_DR;
            if (ui->cursor_visible && ui->cx == x && ui->cy == y)
                tile |= CURSOR;

            if (flashframe >= 0 && ui->flash_type == DEFEAT && flashframe != 1)
                tile |= BADFLASH;

            if (ds->grid[pos] != tile) {
		draw_tile(dr, ds, x, y, tile);
		ds->grid[pos] = tile;
	    }
	}
    }

    sfree(grid);

    {
	char status[255];

        sprintf(status, "%s%d / %d moves",
                (state->complete && state->moves <= state->movelimit ?
                 (state->cheated ? "Auto-solved. " : "COMPLETED! ") :
                 state->moves >= state->movelimit ? "FAILED! " :
                 state->cheated ? "Auto-solver used. " :
                 ""),
                state->moves,
                state->movelimit);

	status_bar(dr, status);
    }
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cursor_visible)
    {
        *x = COORD(ui->cx);
        *y = COORD(ui->cy);
        *w = *h = TILESIZE;
    }
}

static int game_status(const game_state *state)
{
    if (state->complete && state->moves <= state->movelimit) {
        return +1;                     /* victory! */
    } else if (state->moves >= state->movelimit) {
        return -1;                     /* defeat */
    } else {
        return 0;                      /* still playing */
    }
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (dir == +1) {
        int old_status = game_status(oldstate);
        int new_status = game_status(newstate);
        if (old_status != new_status) {
            assert(old_status == 0);

            if (new_status == +1) {
                int frames = newstate->w + newstate->h + newstate->colours - 2;
                ui->flash_type = VICTORY;
                return VICTORY_FLASH_FRAME * frames;
            } else {
                ui->flash_type = DEFEAT;
                return DEFEAT_FLASH_FRAME * 3;
            }
        }
    }
    return 0.0F;
}

#ifdef COMBINED
#define thegame flood
#endif

const struct game thegame = {
    "Flood", "games.flood", "flood",
    default_params,
    game_fetch_preset, NULL,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    true, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    true, solve_game,
    true, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    NULL, /* game_request_keys */
    game_changed_state,
    current_key_label,
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    false, false, NULL, NULL,          /* print_size, print */
    true,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,				       /* flags */
};
