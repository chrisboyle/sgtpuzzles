/*
 * loopy.c:
 *
 * An implementation of the Nikoli game 'Loop the loop'.
 * (c) Mike Pinna, 2005, 2006
 * Substantially rewritten to allowing for more general types of grid.
 * (c) Lambros Lambrou 2008
 *
 * vim: set shiftwidth=4 :set textwidth=80:
 */

/*
 * Possible future solver enhancements:
 * 
 *  - There's an interesting deductive technique which makes use
 *    of topology rather than just graph theory. Each _face_ in
 *    the grid is either inside or outside the loop; you can tell
 *    that two faces are on the same side of the loop if they're
 *    separated by a LINE_NO (or, more generally, by a path
 *    crossing no LINE_UNKNOWNs and an even number of LINE_YESes),
 *    and on the opposite side of the loop if they're separated by
 *    a LINE_YES (or an odd number of LINE_YESes and no
 *    LINE_UNKNOWNs). Oh, and any face separated from the outside
 *    of the grid by a LINE_YES or a LINE_NO is on the inside or
 *    outside respectively. So if you can track this for all
 *    faces, you figure out the state of the line between a pair
 *    once their relative insideness is known.
 *     + The way I envisage this working is simply to keep an edsf
 * 	 of all _faces_, which indicates whether they're on
 * 	 opposite sides of the loop from one another. We also
 * 	 include a special entry in the edsf for the infinite
 * 	 exterior "face".
 *     + So, the simple way to do this is to just go through the
 * 	 edges: every time we see an edge in a state other than
 * 	 LINE_UNKNOWN which separates two faces that aren't in the
 * 	 same edsf class, we can rectify that by merging the
 * 	 classes. Then, conversely, an edge in LINE_UNKNOWN state
 * 	 which separates two faces that _are_ in the same edsf
 * 	 class can immediately have its state determined.
 *     + But you can go one better, if you're prepared to loop
 * 	 over all _pairs_ of edges. Suppose we have edges A and B,
 * 	 which respectively separate faces A1,A2 and B1,B2.
 * 	 Suppose that A,B are in the same edge-edsf class and that
 * 	 A1,B1 (wlog) are in the same face-edsf class; then we can
 * 	 immediately place A2,B2 into the same face-edsf class (as
 * 	 each other, not as A1 and A2) one way round or the other.
 * 	 And conversely again, if A1,B1 are in the same face-edsf
 * 	 class and so are A2,B2, then we can put A,B into the same
 * 	 face-edsf class.
 * 	  * Of course, this deduction requires a quadratic-time
 * 	    loop over all pairs of edges in the grid, so it should
 * 	    be reserved until there's nothing easier left to be
 * 	    done.
 * 
 *  - The generalised grid support has made me (SGT) notice a
 *    possible extension to the loop-avoidance code. When you have
 *    a path of connected edges such that no other edges at all
 *    are incident on any vertex in the middle of the path - or,
 *    alternatively, such that any such edges are already known to
 *    be LINE_NO - then you know those edges are either all
 *    LINE_YES or all LINE_NO. Hence you can mentally merge the
 *    entire path into a single long curly edge for the purposes
 *    of loop avoidance, and look directly at whether or not the
 *    extreme endpoints of the path are connected by some other
 *    route. I find this coming up fairly often when I play on the
 *    octagonal grid setting, so it might be worth implementing in
 *    the solver.
 *
 *  - (Just a speed optimisation.)  Consider some todo list queue where every
 *    time we modify something we mark it for consideration by other bits of
 *    the solver, to save iteration over things that have already been done.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"
#include "grid.h"

/* Debugging options */

/*
#define DEBUG_CACHES
#define SHOW_WORKING
#define DEBUG_DLINES
*/

/* ----------------------------------------------------------------------
 * Struct, enum and function declarations
 */

enum {
    COL_BACKGROUND,
    COL_FOREGROUND,
    COL_LINEUNKNOWN,
    COL_HIGHLIGHT,
    COL_MISTAKE,
    COL_SATISFIED,
    NCOLOURS
};

struct game_state {
    grid *game_grid;

    /* Put -1 in a face that doesn't get a clue */
    signed char *clues;

    /* Array of line states, to store whether each line is
     * YES, NO or UNKNOWN */
    char *lines;

    int solved;
    int cheated;

    /* Used in game_text_format(), so that it knows what type of
     * grid it's trying to render as ASCII text. */
    int grid_type;
};

enum solver_status {
    SOLVER_SOLVED,    /* This is the only solution the solver could find */
    SOLVER_MISTAKE,   /* This is definitely not a solution */
    SOLVER_AMBIGUOUS, /* This _might_ be an ambiguous solution */
    SOLVER_INCOMPLETE /* This may be a partial solution */
};

/* ------ Solver state ------ */
typedef struct normal {
    /* For each dline, store a bitmask for whether we know:
     * (bit 0) at least one is YES
     * (bit 1) at most one is YES */
    char *dlines;
} normal_mode_state;

typedef struct hard {
    int *linedsf;
} hard_mode_state;

typedef struct solver_state {
    game_state *state;
    enum solver_status solver_status;
    /* NB looplen is the number of dots that are joined together at a point, ie a
     * looplen of 1 means there are no lines to a particular dot */
    int *looplen;

    /* caches */
    char *dot_yes_count;
    char *dot_no_count;
    char *face_yes_count;
    char *face_no_count;
    char *dot_solved, *face_solved;
    int *dotdsf;

    normal_mode_state *normal;
    hard_mode_state *hard;
} solver_state;

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */

#define DIFFLIST(A) \
    A(EASY,Easy,e,easy_mode_deductions) \
    A(NORMAL,Normal,n,normal_mode_deductions) \
    A(HARD,Hard,h,hard_mode_deductions)
#define ENUM(upper,title,lower,fn) DIFF_ ## upper,
#define TITLE(upper,title,lower,fn) #title,
#define ENCODE(upper,title,lower,fn) #lower
#define CONFIG(upper,title,lower,fn) ":" #title
#define SOLVER_FN_DECL(upper,title,lower,fn) static int fn(solver_state *);
#define SOLVER_FN(upper,title,lower,fn) &fn,
enum { DIFFLIST(ENUM) DIFF_MAX };
static char const *const diffnames[] = { DIFFLIST(TITLE) };
static char const diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)
DIFFLIST(SOLVER_FN_DECL);
static int (*(solver_fns[]))(solver_state *) = { DIFFLIST(SOLVER_FN) };

struct game_params {
    int w, h;
    int diff;
    int type;

    /* Grid generation is expensive, so keep a (ref-counted) reference to the
     * grid for these parameters, and only generate when required. */
    grid *game_grid;
};

enum line_state { LINE_YES, LINE_UNKNOWN, LINE_NO };

#define OPP(line_state) \
    (2 - line_state)


struct game_drawstate {
    int started;
    int tilesize;
    int flashing;
    char *lines;
    char *clue_error;
    char *clue_satisfied;
};

static char *validate_desc(game_params *params, char *desc);
static int dot_order(const game_state* state, int i, char line_type);
static int face_order(const game_state* state, int i, char line_type);
static solver_state *solve_game_rec(const solver_state *sstate,
                                    int diff);

#ifdef DEBUG_CACHES
static void check_caches(const solver_state* sstate);
#else
#define check_caches(s)
#endif

/* ------- List of grid generators ------- */
#define GRIDLIST(A) \
    A(Squares,grid_new_square) \
    A(Triangular,grid_new_triangular) \
    A(Honeycomb,grid_new_honeycomb) \
    A(Snub-Square,grid_new_snubsquare) \
    A(Cairo,grid_new_cairo) \
    A(Great-Hexagonal,grid_new_greathexagonal) \
    A(Octagonal,grid_new_octagonal) \
    A(Kites,grid_new_kites)

#define GRID_NAME(title,fn) #title,
#define GRID_CONFIG(title,fn) ":" #title
#define GRID_FN(title,fn) &fn,
static char const *const gridnames[] = { GRIDLIST(GRID_NAME) };
#define GRID_CONFIGS GRIDLIST(GRID_CONFIG)
static grid * (*(grid_fns[]))(int w, int h) = { GRIDLIST(GRID_FN) };
static const int NUM_GRID_TYPES = sizeof(grid_fns) / sizeof(grid_fns[0]);

/* Generates a (dynamically allocated) new grid, according to the
 * type and size requested in params.  Does nothing if the grid is already
 * generated.  The allocated grid is owned by the params object, and will be
 * freed in free_params(). */
static void params_generate_grid(game_params *params)
{
    if (!params->game_grid) {
        params->game_grid = grid_fns[params->type](params->w, params->h);
    }
}

/* ----------------------------------------------------------------------
 * Preprocessor magic
 */

/* General constants */
#define PREFERRED_TILE_SIZE 32
#define BORDER(tilesize) ((tilesize) / 2)
#define FLASH_TIME 0.5F

#define BIT_SET(field, bit) ((field) & (1<<(bit)))

#define SET_BIT(field, bit)  (BIT_SET(field, bit) ? FALSE : \
                              ((field) |= (1<<(bit)), TRUE))

#define CLEAR_BIT(field, bit) (BIT_SET(field, bit) ? \
                               ((field) &= ~(1<<(bit)), TRUE) : FALSE)

#define CLUE2CHAR(c) \
    ((c < 0) ? ' ' : c + '0')

/* ----------------------------------------------------------------------
 * General struct manipulation and other straightforward code
 */

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->game_grid = state->game_grid;
    ret->game_grid->refcount++;

    ret->solved = state->solved;
    ret->cheated = state->cheated;

    ret->clues = snewn(state->game_grid->num_faces, signed char);
    memcpy(ret->clues, state->clues, state->game_grid->num_faces);

    ret->lines = snewn(state->game_grid->num_edges, char);
    memcpy(ret->lines, state->lines, state->game_grid->num_edges);

    ret->grid_type = state->grid_type;
    return ret;
}

static void free_game(game_state *state)
{
    if (state) {
        grid_free(state->game_grid);
        sfree(state->clues);
        sfree(state->lines);
        sfree(state);
    }
}

static solver_state *new_solver_state(game_state *state, int diff) {
    int i;
    int num_dots = state->game_grid->num_dots;
    int num_faces = state->game_grid->num_faces;
    int num_edges = state->game_grid->num_edges;
    solver_state *ret = snew(solver_state);

    ret->state = dup_game(state);

    ret->solver_status = SOLVER_INCOMPLETE;

    ret->dotdsf = snew_dsf(num_dots);
    ret->looplen = snewn(num_dots, int);

    for (i = 0; i < num_dots; i++) {
        ret->looplen[i] = 1;
    }

    ret->dot_solved = snewn(num_dots, char);
    ret->face_solved = snewn(num_faces, char);
    memset(ret->dot_solved, FALSE, num_dots);
    memset(ret->face_solved, FALSE, num_faces);

    ret->dot_yes_count = snewn(num_dots, char);
    memset(ret->dot_yes_count, 0, num_dots);
    ret->dot_no_count = snewn(num_dots, char);
    memset(ret->dot_no_count, 0, num_dots);
    ret->face_yes_count = snewn(num_faces, char);
    memset(ret->face_yes_count, 0, num_faces);
    ret->face_no_count = snewn(num_faces, char);
    memset(ret->face_no_count, 0, num_faces);

    if (diff < DIFF_NORMAL) {
        ret->normal = NULL;
    } else {
        ret->normal = snew(normal_mode_state);
        ret->normal->dlines = snewn(2*num_edges, char);
        memset(ret->normal->dlines, 0, 2*num_edges);
    }

    if (diff < DIFF_HARD) {
        ret->hard = NULL;
    } else {
        ret->hard = snew(hard_mode_state);
        ret->hard->linedsf = snew_dsf(state->game_grid->num_edges);
    }

    return ret;
}

static void free_solver_state(solver_state *sstate) {
    if (sstate) {
        free_game(sstate->state);
        sfree(sstate->dotdsf);
        sfree(sstate->looplen);
        sfree(sstate->dot_solved);
        sfree(sstate->face_solved);
        sfree(sstate->dot_yes_count);
        sfree(sstate->dot_no_count);
        sfree(sstate->face_yes_count);
        sfree(sstate->face_no_count);

        if (sstate->normal) {
            sfree(sstate->normal->dlines);
            sfree(sstate->normal);
        }

        if (sstate->hard) {
            sfree(sstate->hard->linedsf);
            sfree(sstate->hard);
        }

        sfree(sstate);
    }
}

static solver_state *dup_solver_state(const solver_state *sstate) {
    game_state *state = sstate->state;
    int num_dots = state->game_grid->num_dots;
    int num_faces = state->game_grid->num_faces;
    int num_edges = state->game_grid->num_edges;
    solver_state *ret = snew(solver_state);

    ret->state = state = dup_game(sstate->state);

    ret->solver_status = sstate->solver_status;

    ret->dotdsf = snewn(num_dots, int);
    ret->looplen = snewn(num_dots, int);
    memcpy(ret->dotdsf, sstate->dotdsf,
           num_dots * sizeof(int));
    memcpy(ret->looplen, sstate->looplen,
           num_dots * sizeof(int));

    ret->dot_solved = snewn(num_dots, char);
    ret->face_solved = snewn(num_faces, char);
    memcpy(ret->dot_solved, sstate->dot_solved, num_dots);
    memcpy(ret->face_solved, sstate->face_solved, num_faces);

    ret->dot_yes_count = snewn(num_dots, char);
    memcpy(ret->dot_yes_count, sstate->dot_yes_count, num_dots);
    ret->dot_no_count = snewn(num_dots, char);
    memcpy(ret->dot_no_count, sstate->dot_no_count, num_dots);

    ret->face_yes_count = snewn(num_faces, char);
    memcpy(ret->face_yes_count, sstate->face_yes_count, num_faces);
    ret->face_no_count = snewn(num_faces, char);
    memcpy(ret->face_no_count, sstate->face_no_count, num_faces);

    if (sstate->normal) {
        ret->normal = snew(normal_mode_state);
        ret->normal->dlines = snewn(2*num_edges, char);
        memcpy(ret->normal->dlines, sstate->normal->dlines,
               2*num_edges);
    } else {
        ret->normal = NULL;
    }

    if (sstate->hard) {
        ret->hard = snew(hard_mode_state);
        ret->hard->linedsf = snewn(num_edges, int);
        memcpy(ret->hard->linedsf, sstate->hard->linedsf,
               num_edges * sizeof(int));
    } else {
        ret->hard = NULL;
    }

    return ret;
}

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

#ifdef SLOW_SYSTEM
    ret->h = 7;
    ret->w = 7;
#else
    ret->h = 10;
    ret->w = 10;
#endif
    ret->diff = DIFF_EASY;
    ret->type = 0;

    ret->game_grid = NULL;

    return ret;
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);

    *ret = *params;                       /* structure copy */
    if (ret->game_grid) {
        ret->game_grid->refcount++;
    }
    return ret;
}

static const game_params presets[] = {
    {  7,  7, DIFF_EASY, 0, NULL },
    {  10,  10, DIFF_EASY, 0, NULL },
    {  7,  7, DIFF_NORMAL, 0, NULL },
    {  10,  10, DIFF_NORMAL, 0, NULL },
    {  7,  7, DIFF_HARD, 0, NULL },
    {  10,  10, DIFF_HARD, 0, NULL },
    {  10,  10, DIFF_HARD, 1, NULL },
    {  12,  10, DIFF_HARD, 2, NULL },
    {  7,  7, DIFF_HARD, 3, NULL },
    {  9,  9, DIFF_HARD, 4, NULL },
    {  5,  4, DIFF_HARD, 5, NULL },
    {  7,  7, DIFF_HARD, 6, NULL },
    {  5,  5, DIFF_HARD, 7, NULL },
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *tmppar;
    char buf[80];

    if (i < 0 || i >= lenof(presets))
        return FALSE;

    tmppar = snew(game_params);
    *tmppar = presets[i];
    *params = tmppar;
    sprintf(buf, "%dx%d %s - %s", tmppar->h, tmppar->w,
            gridnames[tmppar->type], diffnames[tmppar->diff]);
    *name = dupstr(buf);

    return TRUE;
}

static void free_params(game_params *params)
{
    if (params->game_grid) {
        grid_free(params->game_grid);
    }
    sfree(params);
}

static void decode_params(game_params *params, char const *string)
{
    if (params->game_grid) {
        grid_free(params->game_grid);
        params->game_grid = NULL;
    }
    params->h = params->w = atoi(string);
    params->diff = DIFF_EASY;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 't') {
        string++;
        params->type = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'd') {
        int i;
        string++;
        for (i = 0; i < DIFF_MAX; i++)
            if (*string == diffchars[i])
                params->diff = i;
        if (*string) string++;
    }
}

static char *encode_params(game_params *params, int full)
{
    char str[80];
    sprintf(str, "%dx%dt%d", params->w, params->h, params->type);
    if (full)
        sprintf(str + strlen(str), "d%c", diffchars[params->diff]);
    return dupstr(str);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(5, config_item);

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

    ret[2].name = "Grid type";
    ret[2].type = C_CHOICES;
    ret[2].sval = GRID_CONFIGS;
    ret[2].ival = params->type;

    ret[3].name = "Difficulty";
    ret[3].type = C_CHOICES;
    ret[3].sval = DIFFCONFIG;
    ret[3].ival = params->diff;

    ret[4].name = NULL;
    ret[4].type = C_END;
    ret[4].sval = NULL;
    ret[4].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->h = atoi(cfg[1].sval);
    ret->type = cfg[2].ival;
    ret->diff = cfg[3].ival;

    ret->game_grid = NULL;
    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->w < 3 || params->h < 3)
        return "Width and height must both be at least 3";
    if (params->type < 0 || params->type >= NUM_GRID_TYPES)
        return "Illegal grid type";

    /*
     * This shouldn't be able to happen at all, since decode_params
     * and custom_params will never generate anything that isn't
     * within range.
     */
    assert(params->diff < DIFF_MAX);

    return NULL;
}

/* Returns a newly allocated string describing the current puzzle */
static char *state_to_text(const game_state *state)
{
    grid *g = state->game_grid;
    char *retval;
    int num_faces = g->num_faces;
    char *description = snewn(num_faces + 1, char);
    char *dp = description;
    int empty_count = 0;
    int i;

    for (i = 0; i < num_faces; i++) {
        if (state->clues[i] < 0) {
            if (empty_count > 25) {
                dp += sprintf(dp, "%c", (int)(empty_count + 'a' - 1));
                empty_count = 0;
            }
            empty_count++;
        } else {
            if (empty_count) {
                dp += sprintf(dp, "%c", (int)(empty_count + 'a' - 1));
                empty_count = 0;
            }
            dp += sprintf(dp, "%c", (int)CLUE2CHAR(state->clues[i]));
        }
    }

    if (empty_count)
        dp += sprintf(dp, "%c", (int)(empty_count + 'a' - 1));

    retval = dupstr(description);
    sfree(description);

    return retval;
}

/* We require that the params pass the test in validate_params and that the
 * description fills the entire game area */
static char *validate_desc(game_params *params, char *desc)
{
    int count = 0;
    grid *g;
    params_generate_grid(params);
    g = params->game_grid;

    for (; *desc; ++desc) {
        if (*desc >= '0' && *desc <= '9') {
            count++;
            continue;
        }
        if (*desc >= 'a') {
            count += *desc - 'a' + 1;
            continue;
        }
        return "Unknown character in description";
    }

    if (count < g->num_faces)
        return "Description too short for board size";
    if (count > g->num_faces)
        return "Description too long for board size";

    return NULL;
}

/* Sums the lengths of the numbers in range [0,n) */
/* See equivalent function in solo.c for justification of this. */
static int len_0_to_n(int n)
{
    int len = 1; /* Counting 0 as a bit of a special case */
    int i;

    for (i = 1; i < n; i *= 10) {
        len += max(n - i, 0);
    }

    return len;
}

static char *encode_solve_move(const game_state *state)
{
    int len;
    char *ret, *p;
    int i;
    int num_edges = state->game_grid->num_edges;

    /* This is going to return a string representing the moves needed to set
     * every line in a grid to be the same as the ones in 'state'.  The exact
     * length of this string is predictable. */

    len = 1;  /* Count the 'S' prefix */
    /* Numbers in all lines */
    len += len_0_to_n(num_edges);
    /* For each line we also have a letter */
    len += num_edges;

    ret = snewn(len + 1, char);
    p = ret;

    p += sprintf(p, "S");

    for (i = 0; i < num_edges; i++) {
        switch (state->lines[i]) {
	  case LINE_YES:
	    p += sprintf(p, "%dy", i);
	    break;
	  case LINE_NO:
	    p += sprintf(p, "%dn", i);
	    break;
        }
    }

    /* No point in doing sums like that if they're going to be wrong */
    assert(strlen(ret) <= (size_t)len);
    return ret;
}

static game_ui *new_ui(game_state *state)
{
    return NULL;
}

static void free_ui(game_ui *ui)
{
}

static char *encode_ui(game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
}

static void game_compute_size(game_params *params, int tilesize,
                              int *x, int *y)
{
    grid *g;
    int grid_width, grid_height, rendered_width, rendered_height;

    params_generate_grid(params);
    g = params->game_grid;
    grid_width = g->highest_x - g->lowest_x;
    grid_height = g->highest_y - g->lowest_y;
    /* multiply first to minimise rounding error on integer division */
    rendered_width = grid_width * tilesize / g->tilesize;
    rendered_height = grid_height * tilesize / g->tilesize;
    *x = rendered_width + 2 * BORDER(tilesize) + 1;
    *y = rendered_height + 2 * BORDER(tilesize) + 1;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(4 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_FOREGROUND * 3 + 0] = 0.0F;
    ret[COL_FOREGROUND * 3 + 1] = 0.0F;
    ret[COL_FOREGROUND * 3 + 2] = 0.0F;

    ret[COL_LINEUNKNOWN * 3 + 0] = 0.8F;
    ret[COL_LINEUNKNOWN * 3 + 1] = 0.8F;
    ret[COL_LINEUNKNOWN * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 1] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 2] = 1.0F;

    ret[COL_MISTAKE * 3 + 0] = 1.0F;
    ret[COL_MISTAKE * 3 + 1] = 0.0F;
    ret[COL_MISTAKE * 3 + 2] = 0.0F;

    ret[COL_SATISFIED * 3 + 0] = 0.0F;
    ret[COL_SATISFIED * 3 + 1] = 0.0F;
    ret[COL_SATISFIED * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);
    int num_faces = state->game_grid->num_faces;
    int num_edges = state->game_grid->num_edges;

    ds->tilesize = 0;
    ds->started = 0;
    ds->lines = snewn(num_edges, char);
    ds->clue_error = snewn(num_faces, char);
    ds->clue_satisfied = snewn(num_faces, char);
    ds->flashing = 0;

    memset(ds->lines, LINE_UNKNOWN, num_edges);
    memset(ds->clue_error, 0, num_faces);
    memset(ds->clue_satisfied, 0, num_faces);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->clue_error);
    sfree(ds->clue_satisfied);
    sfree(ds->lines);
    sfree(ds);
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
                              int dir, game_ui *ui)
{
    return 0.0F;
}

static int game_can_format_as_text_now(game_params *params)
{
    if (params->type != 0)
        return FALSE;
    return TRUE;
}

static char *game_text_format(game_state *state)
{
    int w, h, W, H;
    int x, y, i;
    int cell_size;
    char *ret;
    grid *g = state->game_grid;
    grid_face *f;

    assert(state->grid_type == 0);

    /* Work out the basic size unit */
    f = g->faces; /* first face */
    assert(f->order == 4);
    /* The dots are ordered clockwise, so the two opposite
     * corners are guaranteed to span the square */
    cell_size = abs(f->dots[0]->x - f->dots[2]->x);

    w = (g->highest_x - g->lowest_x) / cell_size;
    h = (g->highest_y - g->lowest_y) / cell_size;

    /* Create a blank "canvas" to "draw" on */
    W = 2 * w + 2;
    H = 2 * h + 1;
    ret = snewn(W * H + 1, char);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W-1; x++) {
            ret[y*W + x] = ' ';
        }
        ret[y*W + W-1] = '\n';
    }
    ret[H*W] = '\0';

    /* Fill in edge info */
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        /* Cell coordinates, from (0,0) to (w-1,h-1) */
        int x1 = (e->dot1->x - g->lowest_x) / cell_size;
        int x2 = (e->dot2->x - g->lowest_x) / cell_size;
        int y1 = (e->dot1->y - g->lowest_y) / cell_size;
        int y2 = (e->dot2->y - g->lowest_y) / cell_size;
        /* Midpoint, in canvas coordinates (canvas coordinates are just twice
         * cell coordinates) */
        x = x1 + x2;
        y = y1 + y2;
        switch (state->lines[i]) {
	  case LINE_YES:
	    ret[y*W + x] = (y1 == y2) ? '-' : '|';
	    break;
	  case LINE_NO:
	    ret[y*W + x] = 'x';
	    break;
	  case LINE_UNKNOWN:
	    break; /* already a space */
	  default:
	    assert(!"Illegal line state");
        }
    }

    /* Fill in clues */
    for (i = 0; i < g->num_faces; i++) {
	int x1, x2, y1, y2;

        f = g->faces + i;
        assert(f->order == 4);
        /* Cell coordinates, from (0,0) to (w-1,h-1) */
	x1 = (f->dots[0]->x - g->lowest_x) / cell_size;
	x2 = (f->dots[2]->x - g->lowest_x) / cell_size;
	y1 = (f->dots[0]->y - g->lowest_y) / cell_size;
	y2 = (f->dots[2]->y - g->lowest_y) / cell_size;
        /* Midpoint, in canvas coordinates */
        x = x1 + x2;
        y = y1 + y2;
        ret[y*W + x] = CLUE2CHAR(state->clues[i]);
    }
    return ret;
}

/* ----------------------------------------------------------------------
 * Debug code
 */

#ifdef DEBUG_CACHES
static void check_caches(const solver_state* sstate)
{
    int i;
    const game_state *state = sstate->state;
    const grid *g = state->game_grid;

    for (i = 0; i < g->num_dots; i++) {
        assert(dot_order(state, i, LINE_YES) == sstate->dot_yes_count[i]);
        assert(dot_order(state, i, LINE_NO) == sstate->dot_no_count[i]);
    }

    for (i = 0; i < g->num_faces; i++) {
        assert(face_order(state, i, LINE_YES) == sstate->face_yes_count[i]);
        assert(face_order(state, i, LINE_NO) == sstate->face_no_count[i]);
    }
}

#if 0
#define check_caches(s) \
    do { \
        fprintf(stderr, "check_caches at line %d\n", __LINE__); \
        check_caches(s); \
    } while (0)
#endif
#endif /* DEBUG_CACHES */

/* ----------------------------------------------------------------------
 * Solver utility functions
 */

/* Sets the line (with index i) to the new state 'line_new', and updates
 * the cached counts of any affected faces and dots.
 * Returns TRUE if this actually changed the line's state. */
static int solver_set_line(solver_state *sstate, int i,
                           enum line_state line_new
#ifdef SHOW_WORKING
			   , const char *reason
#endif
			   )
{
    game_state *state = sstate->state;
    grid *g;
    grid_edge *e;

    assert(line_new != LINE_UNKNOWN);

    check_caches(sstate);

    if (state->lines[i] == line_new) {
        return FALSE; /* nothing changed */
    }
    state->lines[i] = line_new;

#ifdef SHOW_WORKING
    fprintf(stderr, "solver: set line [%d] to %s (%s)\n",
            i, line_new == LINE_YES ? "YES" : "NO",
            reason);
#endif

    g = state->game_grid;
    e = g->edges + i;

    /* Update the cache for both dots and both faces affected by this. */
    if (line_new == LINE_YES) {
        sstate->dot_yes_count[e->dot1 - g->dots]++;
        sstate->dot_yes_count[e->dot2 - g->dots]++;
        if (e->face1) {
            sstate->face_yes_count[e->face1 - g->faces]++;
        }
        if (e->face2) {
            sstate->face_yes_count[e->face2 - g->faces]++;
        }
    } else {
        sstate->dot_no_count[e->dot1 - g->dots]++;
        sstate->dot_no_count[e->dot2 - g->dots]++;
        if (e->face1) {
            sstate->face_no_count[e->face1 - g->faces]++;
        }
        if (e->face2) {
            sstate->face_no_count[e->face2 - g->faces]++;
        }
    }

    check_caches(sstate);
    return TRUE;
}

#ifdef SHOW_WORKING
#define solver_set_line(a, b, c) \
    solver_set_line(a, b, c, __FUNCTION__)
#endif

/*
 * Merge two dots due to the existence of an edge between them.
 * Updates the dsf tracking equivalence classes, and keeps track of
 * the length of path each dot is currently a part of.
 * Returns TRUE if the dots were already linked, ie if they are part of a
 * closed loop, and false otherwise.
 */
static int merge_dots(solver_state *sstate, int edge_index)
{
    int i, j, len;
    grid *g = sstate->state->game_grid;
    grid_edge *e = g->edges + edge_index;

    i = e->dot1 - g->dots;
    j = e->dot2 - g->dots;

    i = dsf_canonify(sstate->dotdsf, i);
    j = dsf_canonify(sstate->dotdsf, j);

    if (i == j) {
        return TRUE;
    } else {
        len = sstate->looplen[i] + sstate->looplen[j];
        dsf_merge(sstate->dotdsf, i, j);
        i = dsf_canonify(sstate->dotdsf, i);
        sstate->looplen[i] = len;
        return FALSE;
    }
}

/* Merge two lines because the solver has deduced that they must be either
 * identical or opposite.   Returns TRUE if this is new information, otherwise
 * FALSE. */
static int merge_lines(solver_state *sstate, int i, int j, int inverse
#ifdef SHOW_WORKING
                       , const char *reason
#endif
		       )
{
    int inv_tmp;

    assert(i < sstate->state->game_grid->num_edges);
    assert(j < sstate->state->game_grid->num_edges);

    i = edsf_canonify(sstate->hard->linedsf, i, &inv_tmp);
    inverse ^= inv_tmp;
    j = edsf_canonify(sstate->hard->linedsf, j, &inv_tmp);
    inverse ^= inv_tmp;

    edsf_merge(sstate->hard->linedsf, i, j, inverse);

#ifdef SHOW_WORKING
    if (i != j) {
        fprintf(stderr, "%s [%d] [%d] %s(%s)\n",
                __FUNCTION__, i, j,
                inverse ? "inverse " : "", reason);
    }
#endif
    return (i != j);
}

#ifdef SHOW_WORKING
#define merge_lines(a, b, c, d) \
    merge_lines(a, b, c, d, __FUNCTION__)
#endif

/* Count the number of lines of a particular type currently going into the
 * given dot. */
static int dot_order(const game_state* state, int dot, char line_type)
{
    int n = 0;
    grid *g = state->game_grid;
    grid_dot *d = g->dots + dot;
    int i;

    for (i = 0; i < d->order; i++) {
        grid_edge *e = d->edges[i];
        if (state->lines[e - g->edges] == line_type)
            ++n;
    }
    return n;
}

/* Count the number of lines of a particular type currently surrounding the
 * given face */
static int face_order(const game_state* state, int face, char line_type)
{
    int n = 0;
    grid *g = state->game_grid;
    grid_face *f = g->faces + face;
    int i;

    for (i = 0; i < f->order; i++) {
        grid_edge *e = f->edges[i];
        if (state->lines[e - g->edges] == line_type)
            ++n;
    }
    return n;
}

/* Set all lines bordering a dot of type old_type to type new_type
 * Return value tells caller whether this function actually did anything */
static int dot_setall(solver_state *sstate, int dot,
		      char old_type, char new_type)
{
    int retval = FALSE, r;
    game_state *state = sstate->state;
    grid *g;
    grid_dot *d;
    int i;

    if (old_type == new_type)
        return FALSE;

    g = state->game_grid;
    d = g->dots + dot;

    for (i = 0; i < d->order; i++) {
        int line_index = d->edges[i] - g->edges;
        if (state->lines[line_index] == old_type) {
            r = solver_set_line(sstate, line_index, new_type);
            assert(r == TRUE);
            retval = TRUE;
        }
    }
    return retval;
}

/* Set all lines bordering a face of type old_type to type new_type */
static int face_setall(solver_state *sstate, int face,
                       char old_type, char new_type)
{
    int retval = FALSE, r;
    game_state *state = sstate->state;
    grid *g;
    grid_face *f;
    int i;

    if (old_type == new_type)
        return FALSE;

    g = state->game_grid;
    f = g->faces + face;

    for (i = 0; i < f->order; i++) {
        int line_index = f->edges[i] - g->edges;
        if (state->lines[line_index] == old_type) {
            r = solver_set_line(sstate, line_index, new_type);
            assert(r == TRUE);
            retval = TRUE;
        }
    }
    return retval;
}

/* ----------------------------------------------------------------------
 * Loop generation and clue removal
 */

/* We're going to store a list of current candidate faces for lighting.
 * Each face gets a 'score', which tells us how adding that face right
 * now would affect the length of the solution loop.  We're trying to
 * maximise that quantity so will bias our random selection of faces to
 * light towards those with high scores */
struct face {
    int score;
    unsigned long random;
    grid_face *f;
};

static int get_face_cmpfn(void *v1, void *v2)
{
    struct face *f1 = v1;
    struct face *f2 = v2;
    /* These grid_face pointers always point into the same list of
     * 'grid_face's, so it's valid to subtract them. */
    return f1->f - f2->f;
}

static int face_sort_cmpfn(void *v1, void *v2)
{
    struct face *f1 = v1;
    struct face *f2 = v2;
    int r;

    r = f2->score - f1->score;
    if (r) {
        return r;
    }

    if (f1->random < f2->random)
        return -1;
    else if (f1->random > f2->random)
        return 1;

    /*
     * It's _just_ possible that two faces might have been given
     * the same random value. In that situation, fall back to
     * comparing based on the positions within the grid's face-list.
     * This introduces a tiny directional bias, but not a significant one.
     */
    return get_face_cmpfn(f1, f2);
}

enum { FACE_LIT, FACE_UNLIT };

/* face should be of type grid_face* here. */
#define FACE_LIT_STATE(face) \
    ( (face) == NULL ? FACE_UNLIT : \
	  board[(face) - g->faces] )

/* 'board' is an array of these enums, indicating which faces are
 * currently lit.  Returns whether it's legal to light up the
 * given face. */
static int can_light_face(grid *g, char* board, int face_index)
{
    int i, j;
    grid_face *test_face = g->faces + face_index;
    grid_face *starting_face, *current_face;
    int transitions;
    int current_state, s;
    int found_lit_neighbour = FALSE;
    assert(board[face_index] == FACE_UNLIT);

    /* Can only consider a face for lighting if it's adjacent to an
     * already lit face. */
    for (i = 0; i < test_face->order; i++) {
        grid_edge *e = test_face->edges[i];
        grid_face *f = (e->face1 == test_face) ? e->face2 : e->face1;
        if (FACE_LIT_STATE(f) == FACE_LIT) {
            found_lit_neighbour = TRUE;
            break;
        }
    }
    if (!found_lit_neighbour)
        return FALSE;

    /* Need to avoid creating a loop of lit faces around some unlit faces.
     * Also need to avoid meeting another lit face at a corner, with
     * unlit faces in between.  Here's a simple test that (I believe) takes
     * care of both these conditions:
     *
     * Take the circular path formed by this face's edges, and inflate it
     * slightly outwards.  Imagine walking around this path and consider
     * the faces that you visit in sequence.  This will include all faces
     * touching the given face, either along an edge or just at a corner.
     * Count the number of LIT/UNLIT transitions you encounter, as you walk
     * along the complete loop.  This will obviously turn out to be an even
     * number.
     * If 0, we're either in a completely unlit zone, or this face is a hole
     * in a completely lit zone.  If the former, we would create a brand new
     * island by lighting this face.  And the latter ought to be impossible -
     * it would mean there's already a lit loop, so something went wrong
     * earlier.
     * If 4 or greater, there are too many separate lit regions touching this
     * face, and lighting it up would create a loop or a corner-violation.
     * The only allowed case is when the count is exactly 2. */

    /* i points to a dot around the test face.
     * j points to a face around the i^th dot.
     * The current face will always be:
     *     test_face->dots[i]->faces[j]
     * We assume dots go clockwise around the test face,
     * and faces go clockwise around dots. */
    i = j = 0;
    starting_face = test_face->dots[0]->faces[0];
    if (starting_face == test_face) {
        j = 1;
        starting_face = test_face->dots[0]->faces[1];
    }
    current_face = starting_face;
    transitions = 0;
    current_state = FACE_LIT_STATE(current_face);

    do {
        /* Advance to next face.
         * Need to loop here because it might take several goes to
         * find it. */
        while (TRUE) {
            j++;
            if (j == test_face->dots[i]->order)
                j = 0;

            if (test_face->dots[i]->faces[j] == test_face) {
                /* Advance to next dot round test_face, then
                 * find current_face around new dot
                 * and advance to the next face clockwise */
                i++;
                if (i == test_face->order)
                    i = 0;
                for (j = 0; j < test_face->dots[i]->order; j++) {
                    if (test_face->dots[i]->faces[j] == current_face)
                        break;
                }
                /* Must actually find current_face around new dot,
                 * or else something's wrong with the grid. */
                assert(j != test_face->dots[i]->order);
                /* Found, so advance to next face and try again */
            } else {
                break;
            }
        }
        /* (i,j) are now advanced to next face */
        current_face = test_face->dots[i]->faces[j];
        s = FACE_LIT_STATE(current_face);
        if (s != current_state) {
            ++transitions;
            current_state = s;
            if (transitions > 2)
                return FALSE; /* no point in continuing */
        }
    } while (current_face != starting_face);

    return (transitions == 2) ? TRUE : FALSE;
}

/* The 'score' of a face reflects its current desirability for selection
 * as the next face to light.  We want to encourage moving into uncharted
 * areas so we give scores according to how many of the face's neighbours
 * are currently unlit. */
static int face_score(grid *g, char *board, grid_face *face)
{
    /* Simple formula: score = neighbours unlit - neighbours lit */
    int lit_count = 0, unlit_count = 0;
    int i;
    grid_face *f;
    grid_edge *e;
    for (i = 0; i < face->order; i++) {
        e = face->edges[i];
        f = (e->face1 == face) ? e->face2 : e->face1;
        if (FACE_LIT_STATE(f) == FACE_LIT)
            ++lit_count;
        else
            ++unlit_count;
    }
    return unlit_count - lit_count;
}

/* Generate a new complete set of clues for the given game_state. */
static void add_full_clues(game_state *state, random_state *rs)
{
    signed char *clues = state->clues;
    char *board;
    grid *g = state->game_grid;
    int i, j, c;
    int num_faces = g->num_faces;
    int first_time = TRUE;

    struct face *face, *tmpface;
    struct face face_pos;

    /* These will contain exactly the same information, sorted into different
     * orders */
    tree234 *lightable_faces_sorted, *lightable_faces_gettable;

#define IS_LIGHTING_CANDIDATE(i) \
    (board[i] == FACE_UNLIT && \
	 can_light_face(g, board, i))

    board = snewn(num_faces, char);

    /* Make a board */
    memset(board, FACE_UNLIT, num_faces);

    /* We need a way of favouring faces that will increase our loopiness.
     * We do this by maintaining a list of all candidate faces sorted by
     * their score and choose randomly from that with appropriate skew.
     * In order to avoid consistently biasing towards particular faces, we
     * need the sort order _within_ each group of scores to be completely
     * random.  But it would be abusing the hospitality of the tree234 data
     * structure if our comparison function were nondeterministic :-).  So with
     * each face we associate a random number that does not change during a
     * particular run of the generator, and use that as a secondary sort key.
     * Yes, this means we will be biased towards particular random faces in
     * any one run but that doesn't actually matter. */

    lightable_faces_sorted   = newtree234(face_sort_cmpfn);
    lightable_faces_gettable = newtree234(get_face_cmpfn);
#define ADD_FACE(f) \
    do { \
        struct face *x = add234(lightable_faces_sorted, f); \
        assert(x == f); \
        x = add234(lightable_faces_gettable, f); \
        assert(x == f); \
    } while (0)

#define REMOVE_FACE(f) \
    do { \
        struct face *x = del234(lightable_faces_sorted, f); \
        assert(x); \
        x = del234(lightable_faces_gettable, f); \
        assert(x); \
    } while (0)

    /* Light faces one at a time until the board is interesting enough */
    while (TRUE)
    {
        if (first_time) {
            first_time = FALSE;
            /* lightable_faces_xxx are empty, so start the process by
             * lighting up the middle face.  These tree234s should
             * remain empty, consistent with what would happen if
             * first_time were FALSE. */
            board[g->middle_face - g->faces] = FACE_LIT;
            face = snew(struct face);
            face->f = g->middle_face;
            /* No need to initialise any more of 'face' here, no other fields
             * are used in this case. */
        } else {
            /* We have count234(lightable_faces_gettable) possibilities, and in
             * lightable_faces_sorted they are sorted with the most desirable
             * first. */
            c = count234(lightable_faces_sorted);
            if (c == 0)
                break;
            assert(c == count234(lightable_faces_gettable));

            /* Check that the best face available is any good */
            face = (struct face *)index234(lightable_faces_sorted, 0);
            assert(face);

            /*
             * The situation for a general grid is slightly different from
             * a square grid.  Decreasing the perimeter should be allowed
             * sometimes (think about creating a hexagon of lit triangles,
             * for example).  For if it were _never_ done, then the user would
             * be able to illicitly deduce certain things.  So we do it
             * sometimes but not always.
             */
            if (face->score <= 0 && random_upto(rs, 2) == 0) {
                break;
            }

            assert(face->f); /* not the infinite face */
            assert(FACE_LIT_STATE(face->f) == FACE_UNLIT);

            /* Update data structures */
            /* Light up the face and remove it from the lists */
            board[face->f - g->faces] = FACE_LIT;
            REMOVE_FACE(face);
        }

        /* The face we've just lit up potentially affects the lightability
         * of any neighbouring faces (touching at a corner or edge).  So the
         * search needs to be conducted around all faces touching the one
         * we've just lit.  Iterate over its corners, then over each corner's
         * faces. */
        for (i = 0; i < face->f->order; i++) {
            grid_dot *d = face->f->dots[i];
            for (j = 0; j < d->order; j++) {
                grid_face *f2 = d->faces[j];
                if (f2 == NULL)
                    continue;
                if (f2 == face->f)
                    continue;
                face_pos.f = f2;
                tmpface = find234(lightable_faces_gettable, &face_pos, NULL);
                if (tmpface) {
                    assert(tmpface->f == face_pos.f);
                    assert(FACE_LIT_STATE(tmpface->f) == FACE_UNLIT);
                    REMOVE_FACE(tmpface);
                } else {
                    tmpface = snew(struct face);
                    tmpface->f = face_pos.f;
                    tmpface->random = random_bits(rs, 31);
                }
                tmpface->score = face_score(g, board, tmpface->f);

                if (IS_LIGHTING_CANDIDATE(tmpface->f - g->faces)) {
                    ADD_FACE(tmpface);
                } else {
                    sfree(tmpface);
                }
            }
        }
        sfree(face);
    }

    /* Clean up */
    while ((face = delpos234(lightable_faces_gettable, 0)) != NULL)
        sfree(face);
    freetree234(lightable_faces_gettable);
    freetree234(lightable_faces_sorted);

    /* Fill out all the clues by initialising to 0, then iterating over
     * all edges and incrementing each clue as we find edges that border
     * between LIT/UNLIT faces */
    memset(clues, 0, num_faces);
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        grid_face *f1 = e->face1;
        grid_face *f2 = e->face2;
        if (FACE_LIT_STATE(f1) != FACE_LIT_STATE(f2)) {
            if (f1) clues[f1 - g->faces]++;
            if (f2) clues[f2 - g->faces]++;
        }
    }

    sfree(board);
}


static int game_has_unique_soln(const game_state *state, int diff)
{
    int ret;
    solver_state *sstate_new;
    solver_state *sstate = new_solver_state((game_state *)state, diff);

    sstate_new = solve_game_rec(sstate, diff);

    assert(sstate_new->solver_status != SOLVER_MISTAKE);
    ret = (sstate_new->solver_status == SOLVER_SOLVED);

    free_solver_state(sstate_new);
    free_solver_state(sstate);

    return ret;
}


/* Remove clues one at a time at random. */
static game_state *remove_clues(game_state *state, random_state *rs,
                                int diff)
{
    int *face_list;
    int num_faces = state->game_grid->num_faces;
    game_state *ret = dup_game(state), *saved_ret;
    int n;

    /* We need to remove some clues.  We'll do this by forming a list of all
     * available clues, shuffling it, then going along one at a
     * time clearing each clue in turn for which doing so doesn't render the
     * board unsolvable. */
    face_list = snewn(num_faces, int);
    for (n = 0; n < num_faces; ++n) {
        face_list[n] = n;
    }

    shuffle(face_list, num_faces, sizeof(int), rs);

    for (n = 0; n < num_faces; ++n) {
        saved_ret = dup_game(ret);
        ret->clues[face_list[n]] = -1;

        if (game_has_unique_soln(ret, diff)) {
            free_game(saved_ret);
        } else {
            free_game(ret);
            ret = saved_ret;
        }
    }
    sfree(face_list);

    return ret;
}


static char *new_game_desc(game_params *params, random_state *rs,
                           char **aux, int interactive)
{
    /* solution and description both use run-length encoding in obvious ways */
    char *retval;
    grid *g;
    game_state *state = snew(game_state);
    game_state *state_new;
    params_generate_grid(params);
    state->game_grid = g = params->game_grid;
    g->refcount++;
    state->clues = snewn(g->num_faces, signed char);
    state->lines = snewn(g->num_edges, char);

    state->grid_type = params->type;

    newboard_please:

    memset(state->lines, LINE_UNKNOWN, g->num_edges);

    state->solved = state->cheated = FALSE;

    /* Get a new random solvable board with all its clues filled in.  Yes, this
     * can loop for ever if the params are suitably unfavourable, but
     * preventing games smaller than 4x4 seems to stop this happening */
    do {
        add_full_clues(state, rs);
    } while (!game_has_unique_soln(state, params->diff));

    state_new = remove_clues(state, rs, params->diff);
    free_game(state);
    state = state_new;


    if (params->diff > 0 && game_has_unique_soln(state, params->diff-1)) {
#ifdef SHOW_WORKING
        fprintf(stderr, "Rejecting board, it is too easy\n");
#endif
        goto newboard_please;
    }

    retval = state_to_text(state);

    free_game(state);

    assert(!validate_desc(params, retval));

    return retval;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    int i;
    game_state *state = snew(game_state);
    int empties_to_make = 0;
    int n;
    const char *dp = desc;
    grid *g;
    int num_faces, num_edges;

    params_generate_grid(params);
    state->game_grid = g = params->game_grid;
    g->refcount++;
    num_faces = g->num_faces;
    num_edges = g->num_edges;

    state->clues = snewn(num_faces, signed char);
    state->lines = snewn(num_edges, char);

    state->solved = state->cheated = FALSE;

    state->grid_type = params->type;

    for (i = 0; i < num_faces; i++) {
        if (empties_to_make) {
            empties_to_make--;
            state->clues[i] = -1;
            continue;
        }

        assert(*dp);
        n = *dp - '0';
        if (n >= 0 && n < 10) {
            state->clues[i] = n;
        } else {
            n = *dp - 'a' + 1;
            assert(n > 0);
            state->clues[i] = -1;
            empties_to_make = n - 1;
        }
        ++dp;
    }

    memset(state->lines, LINE_UNKNOWN, num_edges);

    return state;
}

enum { LOOP_NONE=0, LOOP_SOLN, LOOP_NOT_SOLN };

/* ----------------------------------------------------------------------
 * Solver logic
 *
 * Our solver modes operate as follows.  Each mode also uses the modes above it.
 *
 *   Easy Mode
 *   Just implement the rules of the game.
 *
 *   Normal Mode
 *   For each (adjacent) pair of lines through each dot we store a bit for
 *   whether at least one of them is on and whether at most one is on.  (If we
 *   know both or neither is on that's already stored more directly.)
 *
 *   Advanced Mode
 *   Use edsf data structure to make equivalence classes of lines that are
 *   known identical to or opposite to one another.
 */


/* DLines:
 * For general grids, we consider "dlines" to be pairs of lines joined
 * at a dot.  The lines must be adjacent around the dot, so we can think of
 * a dline as being a dot+face combination.  Or, a dot+edge combination where
 * the second edge is taken to be the next clockwise edge from the dot.
 * Original loopy code didn't have this extra restriction of the lines being
 * adjacent.  From my tests with square grids, this extra restriction seems to
 * take little, if anything, away from the quality of the puzzles.
 * A dline can be uniquely identified by an edge/dot combination, given that
 * a dline-pair always goes clockwise around its common dot.  The edge/dot
 * combination can be represented by an edge/bool combination - if bool is
 * TRUE, use edge->dot1 else use edge->dot2.  So the total number of dlines is
 * exactly twice the number of edges in the grid - although the dlines
 * spanning the infinite face are not all that useful to the solver.
 * Note that, by convention, a dline goes clockwise around its common dot,
 * which means the dline goes anti-clockwise around its common face.
 */

/* Helper functions for obtaining an index into an array of dlines, given
 * various information.  We assume the grid layout conventions about how
 * the various lists are interleaved - see grid_make_consistent() for
 * details. */

/* i points to the first edge of the dline pair, reading clockwise around
 * the dot. */
static int dline_index_from_dot(grid *g, grid_dot *d, int i)
{
    grid_edge *e = d->edges[i];
    int ret;
#ifdef DEBUG_DLINES
    grid_edge *e2;
    int i2 = i+1;
    if (i2 == d->order) i2 = 0;
    e2 = d->edges[i2];
#endif
    ret = 2 * (e - g->edges) + ((e->dot1 == d) ? 1 : 0);
#ifdef DEBUG_DLINES
    printf("dline_index_from_dot: d=%d,i=%d, edges [%d,%d] - %d\n",
           (int)(d - g->dots), i, (int)(e - g->edges),
           (int)(e2 - g->edges), ret);
#endif
    return ret;
}
/* i points to the second edge of the dline pair, reading clockwise around
 * the face.  That is, the edges of the dline, starting at edge{i}, read
 * anti-clockwise around the face.  By layout conventions, the common dot
 * of the dline will be f->dots[i] */
static int dline_index_from_face(grid *g, grid_face *f, int i)
{
    grid_edge *e = f->edges[i];
    grid_dot *d = f->dots[i];
    int ret;
#ifdef DEBUG_DLINES
    grid_edge *e2;
    int i2 = i - 1;
    if (i2 < 0) i2 += f->order;
    e2 = f->edges[i2];
#endif
    ret = 2 * (e - g->edges) + ((e->dot1 == d) ? 1 : 0);
#ifdef DEBUG_DLINES
    printf("dline_index_from_face: f=%d,i=%d, edges [%d,%d] - %d\n",
           (int)(f - g->faces), i, (int)(e - g->edges),
           (int)(e2 - g->edges), ret);
#endif
    return ret;
}
static int is_atleastone(const char *dline_array, int index)
{
    return BIT_SET(dline_array[index], 0);
}
static int set_atleastone(char *dline_array, int index)
{
    return SET_BIT(dline_array[index], 0);
}
static int is_atmostone(const char *dline_array, int index)
{
    return BIT_SET(dline_array[index], 1);
}
static int set_atmostone(char *dline_array, int index)
{
    return SET_BIT(dline_array[index], 1);
}

static void array_setall(char *array, char from, char to, int len)
{
    char *p = array, *p_old = p;
    int len_remaining = len;

    while ((p = memchr(p, from, len_remaining))) {
        *p = to;
        len_remaining -= p - p_old;
        p_old = p;
    }
}

/* Helper, called when doing dline dot deductions, in the case where we
 * have 4 UNKNOWNs, and two of them (adjacent) have *exactly* one YES between
 * them (because of dline atmostone/atleastone).
 * On entry, edge points to the first of these two UNKNOWNs.  This function
 * will find the opposite UNKNOWNS (if they are adjacent to one another)
 * and set their corresponding dline to atleastone.  (Setting atmostone
 * already happens in earlier dline deductions) */
static int dline_set_opp_atleastone(solver_state *sstate,
                                    grid_dot *d, int edge)
{
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    int N = d->order;
    int opp, opp2;
    for (opp = 0; opp < N; opp++) {
        int opp_dline_index;
        if (opp == edge || opp == edge+1 || opp == edge-1)
            continue;
        if (opp == 0 && edge == N-1)
            continue;
        if (opp == N-1 && edge == 0)
            continue;
        opp2 = opp + 1;
        if (opp2 == N) opp2 = 0;
        /* Check if opp, opp2 point to LINE_UNKNOWNs */
        if (state->lines[d->edges[opp] - g->edges] != LINE_UNKNOWN)
            continue;
        if (state->lines[d->edges[opp2] - g->edges] != LINE_UNKNOWN)
            continue;
        /* Found opposite UNKNOWNS and they're next to each other */
        opp_dline_index = dline_index_from_dot(g, d, opp);
        return set_atleastone(sstate->normal->dlines, opp_dline_index);
    }
    return FALSE;
}


/* Set pairs of lines around this face which are known to be identical, to
 * the given line_state */
static int face_setall_identical(solver_state *sstate, int face_index,
                                 enum line_state line_new)
{
    /* can[dir] contains the canonical line associated with the line in
     * direction dir from the square in question.  Similarly inv[dir] is
     * whether or not the line in question is inverse to its canonical
     * element. */
    int retval = FALSE;
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    grid_face *f = g->faces + face_index;
    int N = f->order;
    int i, j;
    int can1, can2, inv1, inv2;

    for (i = 0; i < N; i++) {
        int line1_index = f->edges[i] - g->edges;
        if (state->lines[line1_index] != LINE_UNKNOWN)
            continue;
        for (j = i + 1; j < N; j++) {
            int line2_index = f->edges[j] - g->edges;
            if (state->lines[line2_index] != LINE_UNKNOWN)
                continue;

            /* Found two UNKNOWNS */
            can1 = edsf_canonify(sstate->hard->linedsf, line1_index, &inv1);
            can2 = edsf_canonify(sstate->hard->linedsf, line2_index, &inv2);
            if (can1 == can2 && inv1 == inv2) {
                solver_set_line(sstate, line1_index, line_new);
                solver_set_line(sstate, line2_index, line_new);
            }
        }
    }
    return retval;
}

/* Given a dot or face, and a count of LINE_UNKNOWNs, find them and
 * return the edge indices into e. */
static void find_unknowns(game_state *state,
    grid_edge **edge_list, /* Edge list to search (from a face or a dot) */
    int expected_count, /* Number of UNKNOWNs (comes from solver's cache) */
    int *e /* Returned edge indices */)
{
    int c = 0;
    grid *g = state->game_grid;
    while (c < expected_count) {
        int line_index = *edge_list - g->edges;
        if (state->lines[line_index] == LINE_UNKNOWN) {
            e[c] = line_index;
            c++;
        }
        ++edge_list;
    }
}

/* If we have a list of edges, and we know whether the number of YESs should
 * be odd or even, and there are only a few UNKNOWNs, we can do some simple
 * linedsf deductions.  This can be used for both face and dot deductions.
 * Returns the difficulty level of the next solver that should be used,
 * or DIFF_MAX if no progress was made. */
static int parity_deductions(solver_state *sstate,
    grid_edge **edge_list, /* Edge list (from a face or a dot) */
    int total_parity, /* Expected number of YESs modulo 2 (either 0 or 1) */
    int unknown_count)
{
    game_state *state = sstate->state;
    int diff = DIFF_MAX;
    int *linedsf = sstate->hard->linedsf;

    if (unknown_count == 2) {
        /* Lines are known alike/opposite, depending on inv. */
        int e[2];
        find_unknowns(state, edge_list, 2, e);
        if (merge_lines(sstate, e[0], e[1], total_parity))
            diff = min(diff, DIFF_HARD);
    } else if (unknown_count == 3) {
        int e[3];
        int can[3]; /* canonical edges */
        int inv[3]; /* whether can[x] is inverse to e[x] */
        find_unknowns(state, edge_list, 3, e);
        can[0] = edsf_canonify(linedsf, e[0], inv);
        can[1] = edsf_canonify(linedsf, e[1], inv+1);
        can[2] = edsf_canonify(linedsf, e[2], inv+2);
        if (can[0] == can[1]) {
            if (solver_set_line(sstate, e[2], (total_parity^inv[0]^inv[1]) ?
				LINE_YES : LINE_NO))
                diff = min(diff, DIFF_EASY);
        }
        if (can[0] == can[2]) {
            if (solver_set_line(sstate, e[1], (total_parity^inv[0]^inv[2]) ?
				LINE_YES : LINE_NO))
                diff = min(diff, DIFF_EASY);
        }
        if (can[1] == can[2]) {
            if (solver_set_line(sstate, e[0], (total_parity^inv[1]^inv[2]) ?
				LINE_YES : LINE_NO))
                diff = min(diff, DIFF_EASY);
        }
    } else if (unknown_count == 4) {
        int e[4];
        int can[4]; /* canonical edges */
        int inv[4]; /* whether can[x] is inverse to e[x] */
        find_unknowns(state, edge_list, 4, e);
        can[0] = edsf_canonify(linedsf, e[0], inv);
        can[1] = edsf_canonify(linedsf, e[1], inv+1);
        can[2] = edsf_canonify(linedsf, e[2], inv+2);
        can[3] = edsf_canonify(linedsf, e[3], inv+3);
        if (can[0] == can[1]) {
            if (merge_lines(sstate, e[2], e[3], total_parity^inv[0]^inv[1]))
                diff = min(diff, DIFF_HARD);
        } else if (can[0] == can[2]) {
            if (merge_lines(sstate, e[1], e[3], total_parity^inv[0]^inv[2]))
                diff = min(diff, DIFF_HARD);
        } else if (can[0] == can[3]) {
            if (merge_lines(sstate, e[1], e[2], total_parity^inv[0]^inv[3]))
                diff = min(diff, DIFF_HARD);
        } else if (can[1] == can[2]) {
            if (merge_lines(sstate, e[0], e[3], total_parity^inv[1]^inv[2]))
                diff = min(diff, DIFF_HARD);
        } else if (can[1] == can[3]) {
            if (merge_lines(sstate, e[0], e[2], total_parity^inv[1]^inv[3]))
                diff = min(diff, DIFF_HARD);
        } else if (can[2] == can[3]) {
            if (merge_lines(sstate, e[0], e[1], total_parity^inv[2]^inv[3]))
                diff = min(diff, DIFF_HARD);
        }
    }
    return diff;
}


/*
 * These are the main solver functions.
 *
 * Their return values are diff values corresponding to the lowest mode solver
 * that would notice the work that they have done.  For example if the normal
 * mode solver adds actual lines or crosses, it will return DIFF_EASY as the
 * easy mode solver might be able to make progress using that.  It doesn't make
 * sense for one of them to return a diff value higher than that of the
 * function itself.
 *
 * Each function returns the lowest value it can, as early as possible, in
 * order to try and pass as much work as possible back to the lower level
 * solvers which progress more quickly.
 */

/* PROPOSED NEW DESIGN:
 * We have a work queue consisting of 'events' notifying us that something has
 * happened that a particular solver mode might be interested in.  For example
 * the hard mode solver might do something that helps the normal mode solver at
 * dot [x,y] in which case it will enqueue an event recording this fact.  Then
 * we pull events off the work queue, and hand each in turn to the solver that
 * is interested in them.  If a solver reports that it failed we pass the same
 * event on to progressively more advanced solvers and the loop detector.  Once
 * we've exhausted an event, or it has helped us progress, we drop it and
 * continue to the next one.  The events are sorted first in order of solver
 * complexity (easy first) then order of insertion (oldest first).
 * Once we run out of events we loop over each permitted solver in turn
 * (easiest first) until either a deduction is made (and an event therefore
 * emerges) or no further deductions can be made (in which case we've failed).
 *
 * QUESTIONS:
 *    * How do we 'loop over' a solver when both dots and squares are concerned.
 *      Answer: first all squares then all dots.
 */

static int easy_mode_deductions(solver_state *sstate)
{
    int i, current_yes, current_no;
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    int diff = DIFF_MAX;

    /* Per-face deductions */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;

        if (sstate->face_solved[i])
            continue;

        current_yes = sstate->face_yes_count[i];
        current_no  = sstate->face_no_count[i];

        if (current_yes + current_no == f->order)  {
            sstate->face_solved[i] = TRUE;
            continue;
        }

        if (state->clues[i] < 0)
            continue;

        if (state->clues[i] < current_yes) {
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
        if (state->clues[i] == current_yes) {
            if (face_setall(sstate, i, LINE_UNKNOWN, LINE_NO))
                diff = min(diff, DIFF_EASY);
            sstate->face_solved[i] = TRUE;
            continue;
        }

        if (f->order - state->clues[i] < current_no) {
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
        if (f->order - state->clues[i] == current_no) {
            if (face_setall(sstate, i, LINE_UNKNOWN, LINE_YES))
                diff = min(diff, DIFF_EASY);
            sstate->face_solved[i] = TRUE;
            continue;
        }
    }

    check_caches(sstate);

    /* Per-dot deductions */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int yes, no, unknown;

        if (sstate->dot_solved[i])
            continue;

        yes = sstate->dot_yes_count[i];
        no = sstate->dot_no_count[i];
        unknown = d->order - yes - no;

        if (yes == 0) {
            if (unknown == 0) {
                sstate->dot_solved[i] = TRUE;
            } else if (unknown == 1) {
                dot_setall(sstate, i, LINE_UNKNOWN, LINE_NO);
                diff = min(diff, DIFF_EASY);
                sstate->dot_solved[i] = TRUE;
            }
        } else if (yes == 1) {
            if (unknown == 0) {
                sstate->solver_status = SOLVER_MISTAKE;
                return DIFF_EASY;
            } else if (unknown == 1) {
                dot_setall(sstate, i, LINE_UNKNOWN, LINE_YES);
                diff = min(diff, DIFF_EASY);
            }
        } else if (yes == 2) {
            if (unknown > 0) {
                dot_setall(sstate, i, LINE_UNKNOWN, LINE_NO);
                diff = min(diff, DIFF_EASY);
            }
            sstate->dot_solved[i] = TRUE;
        } else {
            sstate->solver_status = SOLVER_MISTAKE;
            return DIFF_EASY;
        }
    }

    check_caches(sstate);

    return diff;
}

static int normal_mode_deductions(solver_state *sstate)
{
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    char *dlines = sstate->normal->dlines;
    int i;
    int diff = DIFF_MAX;

    /* ------ Face deductions ------ */

    /* Given a set of dline atmostone/atleastone constraints, need to figure
     * out if we can deduce any further info.  For more general faces than
     * squares, this turns out to be a tricky problem.
     * The approach taken here is to define (per face) NxN matrices:
     * "maxs" and "mins".
     * The entries maxs(j,k) and mins(j,k) define the upper and lower limits
     * for the possible number of edges that are YES between positions j and k
     * going clockwise around the face.  Can think of j and k as marking dots
     * around the face (recall the labelling scheme: edge0 joins dot0 to dot1,
     * edge1 joins dot1 to dot2 etc).
     * Trivially, mins(j,j) = maxs(j,j) = 0, and we don't even bother storing
     * these.  mins(j,j+1) and maxs(j,j+1) are determined by whether edge{j}
     * is YES, NO or UNKNOWN.  mins(j,j+2) and maxs(j,j+2) are related to
     * the dline atmostone/atleastone status for edges j and j+1.
     *
     * Then we calculate the remaining entries recursively.  We definitely
     * know that
     * mins(j,k) >= { mins(j,u) + mins(u,k) } for any u between j and k.
     * This is because any valid placement of YESs between j and k must give
     * a valid placement between j and u, and also between u and k.
     * I believe it's sufficient to use just the two values of u:
     * j+1 and j+2.  Seems to work well in practice - the bounds we compute
     * are rigorous, even if they might not be best-possible.
     *
     * Once we have maxs and mins calculated, we can make inferences about
     * each dline{j,j+1} by looking at the possible complementary edge-counts
     * mins(j+2,j) and maxs(j+2,j) and comparing these with the face clue.
     * As well as dlines, we can make similar inferences about single edges.
     * For example, consider a pentagon with clue 3, and we know at most one
     * of (edge0, edge1) is YES, and at most one of (edge2, edge3) is YES.
     * We could then deduce edge4 is YES, because maxs(0,4) would be 2, so
     * that final edge would have to be YES to make the count up to 3.
     */

    /* Much quicker to allocate arrays on the stack than the heap, so
     * define the largest possible face size, and base our array allocations
     * on that.  We check this with an assertion, in case someone decides to
     * make a grid which has larger faces than this.  Note, this algorithm
     * could get quite expensive if there are many large faces. */
#define MAX_FACE_SIZE 8

    for (i = 0; i < g->num_faces; i++) {
        int maxs[MAX_FACE_SIZE][MAX_FACE_SIZE];
        int mins[MAX_FACE_SIZE][MAX_FACE_SIZE];
        grid_face *f = g->faces + i;
        int N = f->order;
        int j,m;
        int clue = state->clues[i];
        assert(N <= MAX_FACE_SIZE);
        if (sstate->face_solved[i])
            continue;
        if (clue < 0) continue;

        /* Calculate the (j,j+1) entries */
        for (j = 0; j < N; j++) {
            int edge_index = f->edges[j] - g->edges;
            int dline_index;
            enum line_state line1 = state->lines[edge_index];
            enum line_state line2;
            int tmp;
            int k = j + 1;
            if (k >= N) k = 0;
            maxs[j][k] = (line1 == LINE_NO) ? 0 : 1;
            mins[j][k] = (line1 == LINE_YES) ? 1 : 0;
            /* Calculate the (j,j+2) entries */
            dline_index = dline_index_from_face(g, f, k);
            edge_index = f->edges[k] - g->edges;
            line2 = state->lines[edge_index];
            k++;
            if (k >= N) k = 0;

            /* max */
            tmp = 2;
            if (line1 == LINE_NO) tmp--;
            if (line2 == LINE_NO) tmp--;
            if (tmp == 2 && is_atmostone(dlines, dline_index))
                tmp = 1;
            maxs[j][k] = tmp;

            /* min */
            tmp = 0;
            if (line1 == LINE_YES) tmp++;
            if (line2 == LINE_YES) tmp++;
            if (tmp == 0 && is_atleastone(dlines, dline_index))
                tmp = 1;
            mins[j][k] = tmp;
        }

        /* Calculate the (j,j+m) entries for m between 3 and N-1 */
        for (m = 3; m < N; m++) {
            for (j = 0; j < N; j++) {
                int k = j + m;
                int u = j + 1;
                int v = j + 2;
                int tmp;
                if (k >= N) k -= N;
                if (u >= N) u -= N;
                if (v >= N) v -= N;
                maxs[j][k] = maxs[j][u] + maxs[u][k];
                mins[j][k] = mins[j][u] + mins[u][k];
                tmp = maxs[j][v] + maxs[v][k];
                maxs[j][k] = min(maxs[j][k], tmp);
                tmp = mins[j][v] + mins[v][k];
                mins[j][k] = max(mins[j][k], tmp);
            }
        }

        /* See if we can make any deductions */
        for (j = 0; j < N; j++) {
            int k;
            grid_edge *e = f->edges[j];
            int line_index = e - g->edges;
            int dline_index;

            if (state->lines[line_index] != LINE_UNKNOWN)
                continue;
            k = j + 1;
            if (k >= N) k = 0;

            /* minimum YESs in the complement of this edge */
            if (mins[k][j] > clue) {
                sstate->solver_status = SOLVER_MISTAKE;
                return DIFF_EASY;
            }
            if (mins[k][j] == clue) {
                /* setting this edge to YES would make at least
                 * (clue+1) edges - contradiction */
                solver_set_line(sstate, line_index, LINE_NO);
                diff = min(diff, DIFF_EASY);
            }
            if (maxs[k][j] < clue - 1) {
                sstate->solver_status = SOLVER_MISTAKE;
                return DIFF_EASY;
            }
            if (maxs[k][j] == clue - 1) {
                /* Only way to satisfy the clue is to set edge{j} as YES */
                solver_set_line(sstate, line_index, LINE_YES);
                diff = min(diff, DIFF_EASY);
            }

            /* Now see if we can make dline deduction for edges{j,j+1} */
            e = f->edges[k];
            if (state->lines[e - g->edges] != LINE_UNKNOWN)
                /* Only worth doing this for an UNKNOWN,UNKNOWN pair.
                 * Dlines where one of the edges is known, are handled in the
                 * dot-deductions */
                continue;

            dline_index = dline_index_from_face(g, f, k);
            k++;
            if (k >= N) k = 0;

            /* minimum YESs in the complement of this dline */
            if (mins[k][j] > clue - 2) {
                /* Adding 2 YESs would break the clue */
                if (set_atmostone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
            }
            /* maximum YESs in the complement of this dline */
            if (maxs[k][j] < clue) {
                /* Adding 2 NOs would mean not enough YESs */
                if (set_atleastone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
            }
        }
    }

    if (diff < DIFF_NORMAL)
        return diff;

    /* ------ Dot deductions ------ */

    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int N = d->order;
        int yes, no, unknown;
        int j;
        if (sstate->dot_solved[i])
            continue;
        yes = sstate->dot_yes_count[i];
        no = sstate->dot_no_count[i];
        unknown = N - yes - no;

        for (j = 0; j < N; j++) {
            int k;
            int dline_index;
            int line1_index, line2_index;
            enum line_state line1, line2;
            k = j + 1;
            if (k >= N) k = 0;
            dline_index = dline_index_from_dot(g, d, j);
            line1_index = d->edges[j] - g->edges;
            line2_index = d->edges[k] - g->edges;
            line1 = state->lines[line1_index];
            line2 = state->lines[line2_index];

            /* Infer dline state from line state */
            if (line1 == LINE_NO || line2 == LINE_NO) {
                if (set_atmostone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
            }
            if (line1 == LINE_YES || line2 == LINE_YES) {
                if (set_atleastone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
            }
            /* Infer line state from dline state */
            if (is_atmostone(dlines, dline_index)) {
                if (line1 == LINE_YES && line2 == LINE_UNKNOWN) {
                    solver_set_line(sstate, line2_index, LINE_NO);
                    diff = min(diff, DIFF_EASY);
                }
                if (line2 == LINE_YES && line1 == LINE_UNKNOWN) {
                    solver_set_line(sstate, line1_index, LINE_NO);
                    diff = min(diff, DIFF_EASY);
                }
            }
            if (is_atleastone(dlines, dline_index)) {
                if (line1 == LINE_NO && line2 == LINE_UNKNOWN) {
                    solver_set_line(sstate, line2_index, LINE_YES);
                    diff = min(diff, DIFF_EASY);
                }
                if (line2 == LINE_NO && line1 == LINE_UNKNOWN) {
                    solver_set_line(sstate, line1_index, LINE_YES);
                    diff = min(diff, DIFF_EASY);
                }
            }
            /* Deductions that depend on the numbers of lines.
             * Only bother if both lines are UNKNOWN, otherwise the
             * easy-mode solver (or deductions above) would have taken
             * care of it. */
            if (line1 != LINE_UNKNOWN || line2 != LINE_UNKNOWN)
                continue;

            if (yes == 0 && unknown == 2) {
                /* Both these unknowns must be identical.  If we know
                 * atmostone or atleastone, we can make progress. */
                if (is_atmostone(dlines, dline_index)) {
                    solver_set_line(sstate, line1_index, LINE_NO);
                    solver_set_line(sstate, line2_index, LINE_NO);
                    diff = min(diff, DIFF_EASY);
                }
                if (is_atleastone(dlines, dline_index)) {
                    solver_set_line(sstate, line1_index, LINE_YES);
                    solver_set_line(sstate, line2_index, LINE_YES);
                    diff = min(diff, DIFF_EASY);
                }
            }
            if (yes == 1) {
                if (set_atmostone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
                if (unknown == 2) {
                    if (set_atleastone(dlines, dline_index))
                        diff = min(diff, DIFF_NORMAL);
                }
            }

            /* If we have atleastone set for this dline, infer
             * atmostone for each "opposite" dline (that is, each
             * dline without edges in common with this one).
             * Again, this test is only worth doing if both these
             * lines are UNKNOWN.  For if one of these lines were YES,
             * the (yes == 1) test above would kick in instead. */
            if (is_atleastone(dlines, dline_index)) {
                int opp;
                for (opp = 0; opp < N; opp++) {
                    int opp_dline_index;
                    if (opp == j || opp == j+1 || opp == j-1)
                        continue;
                    if (j == 0 && opp == N-1)
                        continue;
                    if (j == N-1 && opp == 0)
                        continue;
                    opp_dline_index = dline_index_from_dot(g, d, opp);
                    if (set_atmostone(dlines, opp_dline_index))
                        diff = min(diff, DIFF_NORMAL);
                }

                if (yes == 0 && is_atmostone(dlines, dline_index)) {
                    /* This dline has *exactly* one YES and there are no
                     * other YESs.  This allows more deductions. */
                    if (unknown == 3) {
                        /* Third unknown must be YES */
                        for (opp = 0; opp < N; opp++) {
                            int opp_index;
                            if (opp == j || opp == k)
                                continue;
                            opp_index = d->edges[opp] - g->edges;
                            if (state->lines[opp_index] == LINE_UNKNOWN) {
                                solver_set_line(sstate, opp_index, LINE_YES);
                                diff = min(diff, DIFF_EASY);
                            }
                        }
                    } else if (unknown == 4) {
                        /* Exactly one of opposite UNKNOWNS is YES.  We've
                         * already set atmostone, so set atleastone as well.
                         */
                        if (dline_set_opp_atleastone(sstate, d, j))
                            diff = min(diff, DIFF_NORMAL);
                    }
                }
            }
        }
    }
    return diff;
}

static int hard_mode_deductions(solver_state *sstate)
{
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    char *dlines = sstate->normal->dlines;
    int i;
    int diff = DIFF_MAX;
    int diff_tmp;

    /* ------ Face deductions ------ */

    /* A fully-general linedsf deduction seems overly complicated
     * (I suspect the problem is NP-complete, though in practice it might just
     * be doable because faces are limited in size).
     * For simplicity, we only consider *pairs* of LINE_UNKNOWNS that are
     * known to be identical.  If setting them both to YES (or NO) would break
     * the clue, set them to NO (or YES). */

    for (i = 0; i < g->num_faces; i++) {
        int N, yes, no, unknown;
        int clue;

        if (sstate->face_solved[i])
            continue;
        clue = state->clues[i];
        if (clue < 0)
            continue;

        N = g->faces[i].order;
        yes = sstate->face_yes_count[i];
        if (yes + 1 == clue) {
            if (face_setall_identical(sstate, i, LINE_NO))
                diff = min(diff, DIFF_EASY);
        }
        no = sstate->face_no_count[i];
        if (no + 1 == N - clue) {
            if (face_setall_identical(sstate, i, LINE_YES))
                diff = min(diff, DIFF_EASY);
        }

        /* Reload YES count, it might have changed */
        yes = sstate->face_yes_count[i];
        unknown = N - no - yes;

        /* Deductions with small number of LINE_UNKNOWNs, based on overall
         * parity of lines. */
        diff_tmp = parity_deductions(sstate, g->faces[i].edges,
                                     (clue - yes) % 2, unknown);
        diff = min(diff, diff_tmp);
    }

    /* ------ Dot deductions ------ */
    for (i = 0; i < g->num_dots; i++) {
        grid_dot *d = g->dots + i;
        int N = d->order;
        int j;
        int yes, no, unknown;
        /* Go through dlines, and do any dline<->linedsf deductions wherever
         * we find two UNKNOWNS. */
        for (j = 0; j < N; j++) {
            int dline_index = dline_index_from_dot(g, d, j);
            int line1_index;
            int line2_index;
            int can1, can2, inv1, inv2;
            int j2;
            line1_index = d->edges[j] - g->edges;
            if (state->lines[line1_index] != LINE_UNKNOWN)
                continue;
            j2 = j + 1;
            if (j2 == N) j2 = 0;
            line2_index = d->edges[j2] - g->edges;
            if (state->lines[line2_index] != LINE_UNKNOWN)
                continue;
            /* Infer dline flags from linedsf */
            can1 = edsf_canonify(sstate->hard->linedsf, line1_index, &inv1);
            can2 = edsf_canonify(sstate->hard->linedsf, line2_index, &inv2);
            if (can1 == can2 && inv1 != inv2) {
                /* These are opposites, so set dline atmostone/atleastone */
                if (set_atmostone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
                if (set_atleastone(dlines, dline_index))
                    diff = min(diff, DIFF_NORMAL);
                continue;
            }
            /* Infer linedsf from dline flags */
            if (is_atmostone(dlines, dline_index)
		&& is_atleastone(dlines, dline_index)) {
                if (merge_lines(sstate, line1_index, line2_index, 1))
                    diff = min(diff, DIFF_HARD);
            }
        }

        /* Deductions with small number of LINE_UNKNOWNs, based on overall
         * parity of lines. */
        yes = sstate->dot_yes_count[i];
        no = sstate->dot_no_count[i];
        unknown = N - yes - no;
        diff_tmp = parity_deductions(sstate, d->edges,
                                     yes % 2, unknown);
        diff = min(diff, diff_tmp);
    }

    /* ------ Edge dsf deductions ------ */

    /* If the state of a line is known, deduce the state of its canonical line
     * too, and vice versa. */
    for (i = 0; i < g->num_edges; i++) {
        int can, inv;
        enum line_state s;
        can = edsf_canonify(sstate->hard->linedsf, i, &inv);
        if (can == i)
            continue;
        s = sstate->state->lines[can];
        if (s != LINE_UNKNOWN) {
            if (solver_set_line(sstate, i, inv ? OPP(s) : s))
                diff = min(diff, DIFF_EASY);
        } else {
            s = sstate->state->lines[i];
            if (s != LINE_UNKNOWN) {
                if (solver_set_line(sstate, can, inv ? OPP(s) : s))
                    diff = min(diff, DIFF_EASY);
            }
        }
    }

    return diff;
}

static int loop_deductions(solver_state *sstate)
{
    int edgecount = 0, clues = 0, satclues = 0, sm1clues = 0;
    game_state *state = sstate->state;
    grid *g = state->game_grid;
    int shortest_chainlen = g->num_dots;
    int loop_found = FALSE;
    int dots_connected;
    int progress = FALSE;
    int i;

    /*
     * Go through the grid and update for all the new edges.
     * Since merge_dots() is idempotent, the simplest way to
     * do this is just to update for _all_ the edges.
     * Also, while we're here, we count the edges.
     */
    for (i = 0; i < g->num_edges; i++) {
        if (state->lines[i] == LINE_YES) {
            loop_found |= merge_dots(sstate, i);
            edgecount++;
        }
    }

    /*
     * Count the clues, count the satisfied clues, and count the
     * satisfied-minus-one clues.
     */
    for (i = 0; i < g->num_faces; i++) {
        int c = state->clues[i];
        if (c >= 0) {
            int o = sstate->face_yes_count[i];
            if (o == c)
                satclues++;
            else if (o == c-1)
                sm1clues++;
            clues++;
        }
    }

    for (i = 0; i < g->num_dots; ++i) {
        dots_connected =
            sstate->looplen[dsf_canonify(sstate->dotdsf, i)];
        if (dots_connected > 1)
            shortest_chainlen = min(shortest_chainlen, dots_connected);
    }

    assert(sstate->solver_status == SOLVER_INCOMPLETE);

    if (satclues == clues && shortest_chainlen == edgecount) {
        sstate->solver_status = SOLVER_SOLVED;
        /* This discovery clearly counts as progress, even if we haven't
         * just added any lines or anything */
        progress = TRUE;
        goto finished_loop_deductionsing;
    }

    /*
     * Now go through looking for LINE_UNKNOWN edges which
     * connect two dots that are already in the same
     * equivalence class. If we find one, test to see if the
     * loop it would create is a solution.
     */
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        int d1 = e->dot1 - g->dots;
        int d2 = e->dot2 - g->dots;
        int eqclass, val;
        if (state->lines[i] != LINE_UNKNOWN)
            continue;

        eqclass = dsf_canonify(sstate->dotdsf, d1);
        if (eqclass != dsf_canonify(sstate->dotdsf, d2))
            continue;

        val = LINE_NO;  /* loop is bad until proven otherwise */

        /*
         * This edge would form a loop. Next
         * question: how long would the loop be?
         * Would it equal the total number of edges
         * (plus the one we'd be adding if we added
         * it)?
         */
        if (sstate->looplen[eqclass] == edgecount + 1) {
            int sm1_nearby;

            /*
             * This edge would form a loop which
             * took in all the edges in the entire
             * grid. So now we need to work out
             * whether it would be a valid solution
             * to the puzzle, which means we have to
             * check if it satisfies all the clues.
             * This means that every clue must be
             * either satisfied or satisfied-minus-
             * 1, and also that the number of
             * satisfied-minus-1 clues must be at
             * most two and they must lie on either
             * side of this edge.
             */
            sm1_nearby = 0;
            if (e->face1) {
                int f = e->face1 - g->faces;
                int c = state->clues[f];
                if (c >= 0 && sstate->face_yes_count[f] == c - 1)
                    sm1_nearby++;
            }
            if (e->face2) {
                int f = e->face2 - g->faces;
                int c = state->clues[f];
                if (c >= 0 && sstate->face_yes_count[f] == c - 1)
                    sm1_nearby++;
            }
            if (sm1clues == sm1_nearby &&
		sm1clues + satclues == clues) {
                val = LINE_YES;  /* loop is good! */
            }
        }

        /*
         * Right. Now we know that adding this edge
         * would form a loop, and we know whether
         * that loop would be a viable solution or
         * not.
         *
         * If adding this edge produces a solution,
         * then we know we've found _a_ solution but
         * we don't know that it's _the_ solution -
         * if it were provably the solution then
         * we'd have deduced this edge some time ago
         * without the need to do loop detection. So
         * in this state we return SOLVER_AMBIGUOUS,
         * which has the effect that hitting Solve
         * on a user-provided puzzle will fill in a
         * solution but using the solver to
         * construct new puzzles won't consider this
         * a reasonable deduction for the user to
         * make.
         */
        progress = solver_set_line(sstate, i, val);
        assert(progress == TRUE);
        if (val == LINE_YES) {
            sstate->solver_status = SOLVER_AMBIGUOUS;
            goto finished_loop_deductionsing;
        }
    }

    finished_loop_deductionsing:
    return progress ? DIFF_EASY : DIFF_MAX;
}

/* This will return a dynamically allocated solver_state containing the (more)
 * solved grid */
static solver_state *solve_game_rec(const solver_state *sstate_start,
                                    int diff)
{
    solver_state *sstate, *sstate_saved;
    int solver_progress;
    game_state *state;

    /* Indicates which solver we should call next.  This is a sensible starting
     * point */
    int current_solver = DIFF_EASY, next_solver;
    sstate = dup_solver_state(sstate_start);

    /* Cache the values of some variables for readability */
    state = sstate->state;

    sstate_saved = NULL;

    solver_progress = FALSE;

    check_caches(sstate);

    do {
        if (sstate->solver_status == SOLVER_MISTAKE)
            return sstate;

        next_solver = solver_fns[current_solver](sstate);

        if (next_solver == DIFF_MAX) {
            if (current_solver < diff && current_solver + 1 < DIFF_MAX) {
                /* Try next beefier solver */
                next_solver = current_solver + 1;
            } else {
                next_solver = loop_deductions(sstate);
            }
        }

        if (sstate->solver_status == SOLVER_SOLVED ||
            sstate->solver_status == SOLVER_AMBIGUOUS) {
/*            fprintf(stderr, "Solver completed\n"); */
            break;
        }

        /* Once we've looped over all permitted solvers then the loop
         * deductions without making any progress, we'll exit this while loop */
        current_solver = next_solver;
    } while (current_solver < DIFF_MAX);

    if (sstate->solver_status == SOLVER_SOLVED ||
        sstate->solver_status == SOLVER_AMBIGUOUS) {
        /* s/LINE_UNKNOWN/LINE_NO/g */
        array_setall(sstate->state->lines, LINE_UNKNOWN, LINE_NO,
                     sstate->state->game_grid->num_edges);
        return sstate;
    }

    return sstate;
}

static char *solve_game(game_state *state, game_state *currstate,
                        char *aux, char **error)
{
    char *soln = NULL;
    solver_state *sstate, *new_sstate;

    sstate = new_solver_state(state, DIFF_MAX);
    new_sstate = solve_game_rec(sstate, DIFF_MAX);

    if (new_sstate->solver_status == SOLVER_SOLVED) {
        soln = encode_solve_move(new_sstate->state);
    } else if (new_sstate->solver_status == SOLVER_AMBIGUOUS) {
        soln = encode_solve_move(new_sstate->state);
        /**error = "Solver found ambiguous solutions"; */
    } else {
        soln = encode_solve_move(new_sstate->state);
        /**error = "Solver failed"; */
    }

    free_solver_state(new_sstate);
    free_solver_state(sstate);

    return soln;
}

/* ----------------------------------------------------------------------
 * Drawing and mouse-handling
 */

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
                            int x, int y, int button)
{
    grid *g = state->game_grid;
    grid_edge *e;
    int i;
    char *ret, buf[80];
    char button_char = ' ';
    enum line_state old_state;

    button &= ~MOD_MASK;

    /* Convert mouse-click (x,y) to grid coordinates */
    x -= BORDER(ds->tilesize);
    y -= BORDER(ds->tilesize);
    x = x * g->tilesize / ds->tilesize;
    y = y * g->tilesize / ds->tilesize;
    x += g->lowest_x;
    y += g->lowest_y;

    e = grid_nearest_edge(g, x, y);
    if (e == NULL)
        return NULL;

    i = e - g->edges;

    /* I think it's only possible to play this game with mouse clicks, sorry */
    /* Maybe will add mouse drag support some time */
    old_state = state->lines[i];

    switch (button) {
      case LEFT_BUTTON:
	switch (old_state) {
	  case LINE_UNKNOWN:
	    button_char = 'y';
	    break;
	  case LINE_YES:
	  case LINE_NO:
	    button_char = 'u';
	    break;
	}
	break;
      case MIDDLE_BUTTON:
	button_char = 'u';
	break;
      case RIGHT_BUTTON:
	switch (old_state) {
	  case LINE_UNKNOWN:
	    button_char = 'n';
	    break;
	  case LINE_NO:
	  case LINE_YES:
	    button_char = 'u';
	    break;
	}
	break;
      default:
	return NULL;
    }


    sprintf(buf, "%d%c", i, (int)button_char);
    ret = dupstr(buf);

    return ret;
}

static game_state *execute_move(game_state *state, char *move)
{
    int i;
    game_state *newstate = dup_game(state);
    grid *g = state->game_grid;

    if (move[0] == 'S') {
        move++;
        newstate->cheated = TRUE;
    }

    while (*move) {
        i = atoi(move);
        move += strspn(move, "1234567890");
        switch (*(move++)) {
	  case 'y':
	    newstate->lines[i] = LINE_YES;
	    break;
	  case 'n':
	    newstate->lines[i] = LINE_NO;
	    break;
	  case 'u':
	    newstate->lines[i] = LINE_UNKNOWN;
	    break;
	  default:
	    goto fail;
        }
    }

    /*
     * Check for completion.
     */
    for (i = 0; i < g->num_edges; i++) {
        if (newstate->lines[i] == LINE_YES)
            break;
    }
    if (i < g->num_edges) {
        int looplen, count;
        grid_edge *start_edge = g->edges + i;
        grid_edge *e = start_edge;
        grid_dot *d = e->dot1;
        /*
         * We've found an edge i. Follow it round
         * to see if it's part of a loop.
         */
        looplen = 0;
        while (1) {
            int j;
            int order = dot_order(newstate, d - g->dots, LINE_YES);
            if (order != 2)
                goto completion_check_done;

            /* Find other edge around this dot */
            for (j = 0; j < d->order; j++) {
                grid_edge *e2 = d->edges[j];
                if (e2 != e && newstate->lines[e2 - g->edges] == LINE_YES)
                    break;
            }
            assert(j != d->order); /* dot_order guarantees success */

            e = d->edges[j];
            d = (e->dot1 == d) ? e->dot2 : e->dot1;
            looplen++;

            if (e == start_edge)
                break;
        }

        /*
         * We've traced our way round a loop, and we know how many
         * line segments were involved. Count _all_ the line
         * segments in the grid, to see if the loop includes them
         * all.
         */
        count = 0;
        for (i = 0; i < g->num_edges; i++) {
            if (newstate->lines[i] == LINE_YES)
                count++;
        }
        assert(count >= looplen);
        if (count != looplen)
            goto completion_check_done;

        /*
         * The grid contains one closed loop and nothing else.
         * Check that all the clues are satisfied.
         */
        for (i = 0; i < g->num_faces; i++) {
            int c = newstate->clues[i];
            if (c >= 0) {
                if (face_order(newstate, i, LINE_YES) != c) {
                    goto completion_check_done;
                }
            }
        }

        /*
         * Completed!
         */
        newstate->solved = TRUE;
    }

    completion_check_done:
    return newstate;

    fail:
    free_game(newstate);
    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

/* Convert from grid coordinates to screen coordinates */
static void grid_to_screen(const game_drawstate *ds, const grid *g,
                           int grid_x, int grid_y, int *x, int *y)
{
    *x = grid_x - g->lowest_x;
    *y = grid_y - g->lowest_y;
    *x = *x * ds->tilesize / g->tilesize;
    *y = *y * ds->tilesize / g->tilesize;
    *x += BORDER(ds->tilesize);
    *y += BORDER(ds->tilesize);
}

/* Returns (into x,y) position of centre of face for rendering the text clue.
 */
static void face_text_pos(const game_drawstate *ds, const grid *g,
                          const grid_face *f, int *x, int *y)
{
    int i;

    /* Simplest solution is the centroid. Might not work in some cases. */

    /* Another algorithm to look into:
     * Find the midpoints of the sides, find the bounding-box,
     * then take the centre of that. */

    /* Best solution probably involves incentres (inscribed circles) */

    int sx = 0, sy = 0; /* sums */
    for (i = 0; i < f->order; i++) {
        grid_dot *d = f->dots[i];
        sx += d->x;
        sy += d->y;
    }
    sx /= f->order;
    sy /= f->order;

    /* convert to screen coordinates */
    grid_to_screen(ds, g, sx, sy, x, y);
}

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
                        game_state *state, int dir, game_ui *ui,
                        float animtime, float flashtime)
{
    grid *g = state->game_grid;
    int border = BORDER(ds->tilesize);
    int i, n;
    char c[2];
    int line_colour, flash_changed;
    int clue_mistake;
    int clue_satisfied;

    if (!ds->started) {
        /*
         * The initial contents of the window are not guaranteed and
         * can vary with front ends. To be on the safe side, all games
         * should start by drawing a big background-colour rectangle
         * covering the whole window.
         */
        int grid_width = g->highest_x - g->lowest_x;
        int grid_height = g->highest_y - g->lowest_y;
        int w = grid_width * ds->tilesize / g->tilesize;
        int h = grid_height * ds->tilesize / g->tilesize;
        draw_rect(dr, 0, 0, w + 2 * border, h + 2 * border, COL_BACKGROUND);

        /* Draw clues */
        for (i = 0; i < g->num_faces; i++) {
	    grid_face *f;
            int x, y;

            c[0] = CLUE2CHAR(state->clues[i]);
            c[1] = '\0';
            f = g->faces + i;
            face_text_pos(ds, g, f, &x, &y);
            draw_text(dr, x, y, FONT_VARIABLE, ds->tilesize/2,
                      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_FOREGROUND, c);
        }
        draw_update(dr, 0, 0, w + 2 * border, h + 2 * border);
    }

    if (flashtime > 0 &&
        (flashtime <= FLASH_TIME/3 ||
         flashtime >= FLASH_TIME*2/3)) {
        flash_changed = !ds->flashing;
        ds->flashing = TRUE;
    } else {
        flash_changed = ds->flashing;
        ds->flashing = FALSE;
    }

    /* Some platforms may perform anti-aliasing, which may prevent clean
     * repainting of lines when the colour is changed.
     * If a line needs to be over-drawn in a different colour, erase a
     * bounding-box around the line, then flag all nearby objects for redraw.
     */
    if (ds->started) {
        const char redraw_flag = 1<<7;
        for (i = 0; i < g->num_edges; i++) {
            /* If we're changing state, AND
             * the previous state was a coloured line */
            if ((state->lines[i] != (ds->lines[i] & ~redraw_flag)) &&
		((ds->lines[i] & ~redraw_flag) != LINE_NO)) {
                grid_edge *e = g->edges + i;
                int x1 = e->dot1->x;
                int y1 = e->dot1->y;
                int x2 = e->dot2->x;
                int y2 = e->dot2->y;
                int xmin, xmax, ymin, ymax;
                int j;
                grid_to_screen(ds, g, x1, y1, &x1, &y1);
                grid_to_screen(ds, g, x2, y2, &x2, &y2);
                /* Allow extra margin for dots, and thickness of lines */
                xmin = min(x1, x2) - 2;
                xmax = max(x1, x2) + 2;
                ymin = min(y1, y2) - 2;
                ymax = max(y1, y2) + 2;
                /* For testing, I find it helpful to change COL_BACKGROUND
                 * to COL_SATISFIED here. */
                draw_rect(dr, xmin, ymin, xmax - xmin + 1, ymax - ymin + 1,
                          COL_BACKGROUND);
                draw_update(dr, xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);

                /* Mark nearby lines for redraw */
                for (j = 0; j < e->dot1->order; j++)
                    ds->lines[e->dot1->edges[j] - g->edges] |= redraw_flag;
                for (j = 0; j < e->dot2->order; j++)
                    ds->lines[e->dot2->edges[j] - g->edges] |= redraw_flag;
                /* Mark nearby clues for redraw.  Use a value that is
                 * neither TRUE nor FALSE for this. */
                if (e->face1)
                    ds->clue_error[e->face1 - g->faces] = 2;
                if (e->face2)
                    ds->clue_error[e->face2 - g->faces] = 2;
            }
        }
    }

    /* Redraw clue colours if necessary */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        int sides = f->order;
        int j;
        n = state->clues[i];
        if (n < 0)
            continue;

        c[0] = CLUE2CHAR(n);
        c[1] = '\0';

        clue_mistake = (face_order(state, i, LINE_YES) > n ||
                        face_order(state, i, LINE_NO ) > (sides-n));

        clue_satisfied = (face_order(state, i, LINE_YES) == n &&
                          face_order(state, i, LINE_NO ) == (sides-n));

        if (clue_mistake != ds->clue_error[i]
	    || clue_satisfied != ds->clue_satisfied[i]) {
            int x, y;
            face_text_pos(ds, g, f, &x, &y);
            /* There seems to be a certain amount of trial-and-error
             * involved in working out the correct bounding-box for
             * the text. */
            draw_rect(dr, x - ds->tilesize/4 - 1, y - ds->tilesize/4 - 3,
                      ds->tilesize/2 + 2, ds->tilesize/2 + 5,
                      COL_BACKGROUND);
            draw_text(dr, x, y,
                      FONT_VARIABLE, ds->tilesize/2,
                      ALIGN_VCENTRE | ALIGN_HCENTRE,
                      clue_mistake ? COL_MISTAKE :
                      clue_satisfied ? COL_SATISFIED : COL_FOREGROUND, c);
            draw_update(dr, x - ds->tilesize/4 - 1, y - ds->tilesize/4 - 3,
                        ds->tilesize/2 + 2, ds->tilesize/2 + 5);

            ds->clue_error[i] = clue_mistake;
            ds->clue_satisfied[i] = clue_satisfied;

            /* Sometimes, the bounding-box encroaches into the surrounding
             * lines (particularly if the window is resized fairly small).
             * So redraw them. */
            for (j = 0; j < f->order; j++)
                ds->lines[f->edges[j] - g->edges] = -1;
        }
    }

    /* I've also had a request to colour lines red if they make a non-solution
     * loop, or if more than two lines go into any point.  I think that would
     * be good some time. */

    /* Lines */
    for (i = 0; i < g->num_edges; i++) {
        grid_edge *e = g->edges + i;
        int x1, x2, y1, y2;
        int xmin, ymin, xmax, ymax;
        int need_draw = (state->lines[i] != ds->lines[i]) ? TRUE : FALSE;
        if (flash_changed && (state->lines[i] == LINE_YES))
            need_draw = TRUE;
        if (!ds->started)
            need_draw = TRUE; /* draw everything at the start */
        ds->lines[i] = state->lines[i];
        if (!need_draw)
            continue;
        if (state->lines[i] == LINE_UNKNOWN)
            line_colour = COL_LINEUNKNOWN;
        else if (state->lines[i] == LINE_NO)
            line_colour = COL_BACKGROUND;
        else if (ds->flashing)
            line_colour = COL_HIGHLIGHT;
        else
            line_colour = COL_FOREGROUND;

        /* Convert from grid to screen coordinates */
        grid_to_screen(ds, g, e->dot1->x, e->dot1->y, &x1, &y1);
        grid_to_screen(ds, g, e->dot2->x, e->dot2->y, &x2, &y2);

        xmin = min(x1, x2);
        xmax = max(x1, x2);
        ymin = min(y1, y2);
        ymax = max(y1, y2);

        if (line_colour != COL_BACKGROUND) {
            /* (dx, dy) points roughly from (x1, y1) to (x2, y2).
             * The line is then "fattened" in a (roughly) perpendicular
             * direction to create a thin rectangle. */
            int dx = (x1 > x2) ? -1 : ((x1 < x2) ? 1 : 0);
            int dy = (y1 > y2) ? -1 : ((y1 < y2) ? 1 : 0);
            int points[] = {
                x1 + dy, y1 - dx,
                x1 - dy, y1 + dx,
                x2 - dy, y2 + dx,
                x2 + dy, y2 - dx
            };
            draw_polygon(dr, points, 4, line_colour, line_colour);
        }
        if (ds->started) {
            /* Draw dots at ends of the line */
            draw_circle(dr, x1, y1, 2, COL_FOREGROUND, COL_FOREGROUND);
            draw_circle(dr, x2, y2, 2, COL_FOREGROUND, COL_FOREGROUND);
        }
        draw_update(dr, xmin-2, ymin-2, xmax - xmin + 4, ymax - ymin + 4);
    }

    /* Draw dots */
    if (!ds->started) {
        for (i = 0; i < g->num_dots; i++) {
            grid_dot *d = g->dots + i;
            int x, y;
            grid_to_screen(ds, g, d->x, d->y, &x, &y);
            draw_circle(dr, x, y, 2, COL_FOREGROUND, COL_FOREGROUND);
        }
    }
    ds->started = TRUE;
}

static float game_flash_length(game_state *oldstate, game_state *newstate,
                               int dir, game_ui *ui)
{
    if (!oldstate->solved  &&  newstate->solved &&
        !oldstate->cheated && !newstate->cheated) {
        return FLASH_TIME;
    }

    return 0.0F;
}

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 7mm "squares" by default.
     */
    game_compute_size(params, 700, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int ink = print_mono_colour(dr, 0);
    int i;
    game_drawstate ads, *ds = &ads;
    grid *g = state->game_grid;

    game_set_size(dr, ds, NULL, tilesize);

    for (i = 0; i < g->num_dots; i++) {
        int x, y;
        grid_to_screen(ds, g, g->dots[i].x, g->dots[i].y, &x, &y);
        draw_circle(dr, x, y, ds->tilesize / 15, ink, ink);
    }

    /*
     * Clues.
     */
    for (i = 0; i < g->num_faces; i++) {
        grid_face *f = g->faces + i;
        int clue = state->clues[i];
        if (clue >= 0) {
            char c[2];
            int x, y;
            c[0] = CLUE2CHAR(clue);
            c[1] = '\0';
            face_text_pos(ds, g, f, &x, &y);
            draw_text(dr, x, y,
                      FONT_VARIABLE, ds->tilesize / 2,
                      ALIGN_VCENTRE | ALIGN_HCENTRE, ink, c);
        }
    }

    /*
     * Lines.
     */
    for (i = 0; i < g->num_edges; i++) {
        int thickness = (state->lines[i] == LINE_YES) ? 30 : 150;
        grid_edge *e = g->edges + i;
        int x1, y1, x2, y2;
        grid_to_screen(ds, g, e->dot1->x, e->dot1->y, &x1, &y1);
        grid_to_screen(ds, g, e->dot2->x, e->dot2->y, &x2, &y2);
        if (state->lines[i] == LINE_YES)
        {
            /* (dx, dy) points from (x1, y1) to (x2, y2).
             * The line is then "fattened" in a perpendicular
             * direction to create a thin rectangle. */
            double d = sqrt(SQ((double)x1 - x2) + SQ((double)y1 - y2));
            double dx = (x2 - x1) / d;
            double dy = (y2 - y1) / d;
	    int points[8];

            dx = (dx * ds->tilesize) / thickness;
            dy = (dy * ds->tilesize) / thickness;
	    points[0] = x1 + dy;
	    points[1] = y1 - dx;
	    points[2] = x1 - dy;
	    points[3] = y1 + dx;
	    points[4] = x2 - dy;
	    points[5] = y2 + dx;
	    points[6] = x2 + dy;
	    points[7] = y2 - dx;
            draw_polygon(dr, points, 4, ink, ink);
        }
        else
        {
            /* Draw a dotted line */
            int divisions = 6;
            int j;
            for (j = 1; j < divisions; j++) {
                /* Weighted average */
                int x = (x1 * (divisions -j) + x2 * j) / divisions;
                int y = (y1 * (divisions -j) + y2 * j) / divisions;
                draw_circle(dr, x, y, ds->tilesize / thickness, ink, ink);
            }
        }
    }
}

#ifdef COMBINED
#define thegame loopy
#endif

const struct game thegame = {
    "Loopy", "games.loopy", "loopy",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    1, solve_game,
    TRUE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    TRUE, FALSE, game_print_size, game_print,
    FALSE /* wants_statusbar */,
    FALSE, game_timing_state,
    0,                                       /* mouse_priorities */
};
