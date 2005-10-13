/*
 * loopy.c: An implementation of the Nikoli game 'Loop the loop'.
 * (c) Mike Pinna, 2005
 *
 * vim: set shiftwidth=4 :set textwidth=80:
 */ 

/*
 * TODO:
 *
 *  - setting very high recursion depth seems to cause memory
 *    munching: are we recursing before checking completion, by any
 *    chance?
 *
 *  - there's an interesting deductive technique which makes use of
 *    topology rather than just graph theory. Each _square_ in the
 *    grid is either inside or outside the loop; you can tell that
 *    two squares are on the same side of the loop if they're
 *    separated by an x (or, more generally, by a path crossing no
 *    LINE_UNKNOWNs and an even number of LINE_YESes), and on the
 *    opposite side of the loop if they're separated by a line (or
 *    an odd number of LINE_YESes and no LINE_UNKNOWNs). Oh, and
 *    any square separated from the outside of the grid by a
 *    LINE_YES or a LINE_NO is on the inside or outside
 *    respectively. So if you can track this for all squares, you
 *    can occasionally spot that two squares are separated by a
 *    LINE_UNKNOWN but their relative insideness is known, and
 *    therefore deduce the state of the edge between them.
 *     + An efficient way to track this would be by augmenting the
 * 	 disjoint set forest data structure. Each element, along
 * 	 with a pointer to a parent member of its equivalence
 * 	 class, would also carry a one-bit field indicating whether
 * 	 it was equal or opposite to its parent. Then you could
 * 	 keep flipping a bit as you ascended the tree during
 * 	 dsf_canonify(), and hence you'd be able to return the
 * 	 relationship of the input value to its ultimate parent
 * 	 (and also you could then get all those bits right when you
 * 	 went back up the tree rewriting). So you'd be able to
 * 	 query whether any two elements were known-equal,
 * 	 known-opposite, or not-known, and you could add new
 * 	 equalities or oppositenesses to increase your knowledge.
 * 	 (Of course the algorithm would have to fail an assertion
 * 	 if you tried to tell it two things it already knew to be
 * 	 opposite were equal, or vice versa!)
 * 	 This data structure would also be useful in the
 * 	 graph-theoretic part of the solver, where it could be used
 * 	 for storing information about which lines are known-identical
 * 	 or known-opposite.  (For example if two lines bordering a 3
 * 	 are known-identical they must both be LINE_YES, and if they
 * 	 are known-opposite, the *other* two lines bordering that clue
 * 	 must be LINE_YES, etc).  This may duplicate some
 * 	 functionality already present in the solver but it is more
 * 	 general and we could remove the old code, so that's no bad
 * 	 thing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define LINEWIDTH (ds->linewidth)
#define BORDER (TILE_SIZE / 2)

#define FLASH_TIME 0.5F

#define HL_COUNT(state) ((state)->w * ((state)->h + 1))
#define VL_COUNT(state) (((state)->w + 1) * (state)->h)
#define DOT_COUNT(state) (((state)->w + 1) * ((state)->h + 1))
#define SQUARE_COUNT(state) ((state)->w * (state)->h)

#define ABOVE_SQUARE(state, i, j) ((state)->hl[(i) + (state)->w * (j)])
#define BELOW_SQUARE(state, i, j) ABOVE_SQUARE(state, i, (j)+1)

#define LEFTOF_SQUARE(state, i, j)  ((state)->vl[(i) + ((state)->w + 1) * (j)])
#define RIGHTOF_SQUARE(state, i, j) LEFTOF_SQUARE(state, (i)+1, j)

#define LEGAL_DOT(state, i, j) ((i) >= 0 && (j) >= 0 &&                 \
                                (i) <= (state)->w && (j) <= (state)->h)

/*
 * These macros return rvalues only, but can cope with being passed
 * out-of-range coordinates.
 */
#define ABOVE_DOT(state, i, j) ((!LEGAL_DOT(state, i, j) || j <= 0) ?  \
                                LINE_NO : LV_ABOVE_DOT(state, i, j))
#define BELOW_DOT(state, i, j) ((!LEGAL_DOT(state, i, j) || j >= (state)->h) ? \
                                LINE_NO : LV_BELOW_DOT(state, i, j))

#define LEFTOF_DOT(state, i, j)  ((!LEGAL_DOT(state, i, j) || i <= 0) ? \
                                  LINE_NO : LV_LEFTOF_DOT(state, i, j))
#define RIGHTOF_DOT(state, i, j) ((!LEGAL_DOT(state, i, j) || i >= (state)->w)?\
                                  LINE_NO : LV_RIGHTOF_DOT(state, i, j))

/*
 * These macros expect to be passed valid coordinates, and return
 * lvalues.
 */
#define LV_BELOW_DOT(state, i, j) ((state)->vl[(i) + ((state)->w + 1) * (j)])
#define LV_ABOVE_DOT(state, i, j) LV_BELOW_DOT(state, i, (j)-1)

#define LV_RIGHTOF_DOT(state, i, j) ((state)->hl[(i) + (state)->w * (j)])
#define LV_LEFTOF_DOT(state, i, j)  LV_RIGHTOF_DOT(state, (i)-1, j)

#define CLUE_AT(state, i, j) ((i < 0 || i >= (state)->w || \
                               j < 0 || j >= (state)->h) ? \
                             ' ' : LV_CLUE_AT(state, i, j))
                             
#define LV_CLUE_AT(state, i, j) ((state)->clues[(i) + (state)->w * (j)])

#define OPP(dir) (dir == LINE_UNKNOWN ? LINE_UNKNOWN : \
                  dir == LINE_YES ? LINE_NO : LINE_YES)

#define BIT_SET(field, bit) ((field) & (1<<(bit)))

#define SET_BIT(field, bit)  (BIT_SET(field, bit) ? FALSE : \
                              ((field) |= (1<<(bit)), TRUE))

#define CLEAR_BIT(field, bit) (BIT_SET(field, bit) ?        \
                               ((field) &= ~(1<<(bit)), TRUE) : FALSE)

static char *game_text_format(game_state *state);

enum {
    COL_BACKGROUND,
    COL_FOREGROUND,
    COL_HIGHLIGHT,
    COL_MISTAKE,
    NCOLOURS
};

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */
#define DIFFLIST(A) \
    A(EASY,Easy,e) \
    A(NORMAL,Normal,n)
#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
/* static char const *const loopy_diffnames[] = { DIFFLIST(TITLE) }; */
static char const loopy_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

/* LINE_YES_ERROR is only used in the drawing routine */
enum line_state { LINE_UNKNOWN, LINE_YES, LINE_NO /*, LINE_YES_ERROR*/ };

enum direction { UP, DOWN, LEFT, RIGHT };

struct game_params {
    int w, h, diff, rec;
};

struct game_state {
    int w, h;
    
    /* Put ' ' in a square that doesn't get a clue */
    char *clues;
    
    /* Arrays of line states, stored left-to-right, top-to-bottom */
    char *hl, *vl;

    int solved;
    int cheated;

    int recursion_depth;
};

static game_state *dup_game(game_state *state)
{
    game_state *ret = snew(game_state);

    ret->h = state->h;
    ret->w = state->w;
    ret->solved = state->solved;
    ret->cheated = state->cheated;

    ret->clues   = snewn(SQUARE_COUNT(state), char);
    memcpy(ret->clues, state->clues, SQUARE_COUNT(state));

    ret->hl      = snewn(HL_COUNT(state), char);
    memcpy(ret->hl, state->hl, HL_COUNT(state));

    ret->vl      = snewn(VL_COUNT(state), char);
    memcpy(ret->vl, state->vl, VL_COUNT(state));

    ret->recursion_depth = state->recursion_depth;

    return ret;
}

static void free_game(game_state *state)
{
    if (state) {
        sfree(state->clues);
        sfree(state->hl);
        sfree(state->vl);
        sfree(state);
    }
}

enum solver_status {
    SOLVER_SOLVED,    /* This is the only solution the solver could find */
    SOLVER_MISTAKE,   /* This is definitely not a solution */
    SOLVER_AMBIGUOUS, /* This _might_ be an ambiguous solution */
    SOLVER_INCOMPLETE /* This may be a partial solution */
};

typedef struct solver_state {
  game_state *state;
  char *dot_atleastone;
  char *dot_atmostone;
/*   char *dline_identical; */
  int recursion_remaining;
  enum solver_status solver_status;
  /* NB looplen is the number of dots that are joined together at a point, ie a
   * looplen of 1 means there are no lines to a particular dot */
  int *dotdsf, *looplen;
} solver_state;

static solver_state *new_solver_state(game_state *state) {
    solver_state *ret = snew(solver_state);
    int i;

    ret->state = dup_game(state);
    
    ret->dot_atmostone = snewn(DOT_COUNT(state), char);
    memset(ret->dot_atmostone, 0, DOT_COUNT(state));
    ret->dot_atleastone = snewn(DOT_COUNT(state), char);
    memset(ret->dot_atleastone, 0, DOT_COUNT(state));

#if 0
    dline_identical = snewn(DOT_COUNT(state), char);
    memset(dline_identical, 0, DOT_COUNT(state));
#endif

    ret->recursion_remaining = state->recursion_depth;
    ret->solver_status = SOLVER_INCOMPLETE; 

    ret->dotdsf = snewn(DOT_COUNT(state), int);
    ret->looplen = snewn(DOT_COUNT(state), int);
    for (i = 0; i < DOT_COUNT(state); i++) {
	ret->dotdsf[i] = i;
	ret->looplen[i] = 1;
    }

    return ret;
}

static void free_solver_state(solver_state *sstate) {
    if (sstate) {
        free_game(sstate->state);
        sfree(sstate->dot_atleastone);
        sfree(sstate->dot_atmostone);
        /*    sfree(sstate->dline_identical); */
        sfree(sstate->dotdsf);
        sfree(sstate->looplen);
        sfree(sstate);
    }
}

static solver_state *dup_solver_state(solver_state *sstate) {
    game_state *state;

    solver_state *ret = snew(solver_state);

    ret->state = state = dup_game(sstate->state);

    ret->dot_atmostone = snewn(DOT_COUNT(state), char);
    memcpy(ret->dot_atmostone, sstate->dot_atmostone, DOT_COUNT(state));

    ret->dot_atleastone = snewn(DOT_COUNT(state), char);
    memcpy(ret->dot_atleastone, sstate->dot_atleastone, DOT_COUNT(state));

#if 0
    ret->dline_identical = snewn((state->w + 1) * (state->h + 1), char);
    memcpy(ret->dline_identical, state->dot_atmostone, 
           (state->w + 1) * (state->h + 1));
#endif

    ret->recursion_remaining = sstate->recursion_remaining;
    ret->solver_status = sstate->solver_status;

    ret->dotdsf = snewn(DOT_COUNT(state), int);
    ret->looplen = snewn(DOT_COUNT(state), int);
    memcpy(ret->dotdsf, sstate->dotdsf, DOT_COUNT(state) * sizeof(int));
    memcpy(ret->looplen, sstate->looplen, DOT_COUNT(state) * sizeof(int));

    return ret;
}

/*
 * Merge two dots due to the existence of an edge between them.
 * Updates the dsf tracking equivalence classes, and keeps track of
 * the length of path each dot is currently a part of.
 * Returns TRUE if the dots were already linked, ie if they are part of a
 * closed loop, and false otherwise.
 */
static int merge_dots(solver_state *sstate, int x1, int y1, int x2, int y2)
{
    int i, j, len;

    i = y1 * (sstate->state->w + 1) + x1;
    j = y2 * (sstate->state->w + 1) + x2;

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

/* Count the number of lines of a particular type currently going into the
 * given dot.  Lines going off the edge of the board are assumed fixed no. */
static int dot_order(const game_state* state, int i, int j, char line_type)
{
    int n = 0;

    if (i > 0) {
        if (LEFTOF_DOT(state, i, j) == line_type)
            ++n;
    } else {
        if (line_type == LINE_NO)
            ++n;
    }
    if (i < state->w) {
        if (RIGHTOF_DOT(state, i, j) == line_type)
            ++n;
    } else {
        if (line_type == LINE_NO)
            ++n;
    }
    if (j > 0) {
        if (ABOVE_DOT(state, i, j) == line_type)
            ++n;
    } else {
        if (line_type == LINE_NO)
            ++n;
    }
    if (j < state->h) {
        if (BELOW_DOT(state, i, j) == line_type)
            ++n;
    } else {
        if (line_type == LINE_NO)
            ++n;
    }

    return n;
}
/* Count the number of lines of a particular type currently surrounding the
 * given square */
static int square_order(const game_state* state, int i, int j, char line_type)
{
    int n = 0;

    if (ABOVE_SQUARE(state, i, j) == line_type)
        ++n;
    if (BELOW_SQUARE(state, i, j) == line_type)
        ++n;
    if (LEFTOF_SQUARE(state, i, j) == line_type)
        ++n;
    if (RIGHTOF_SQUARE(state, i, j) == line_type)
        ++n;

    return n;
}

/* Set all lines bordering a dot of type old_type to type new_type 
 * Return value tells caller whether this function actually did anything */
static int dot_setall(game_state *state, int i, int j,
                       char old_type, char new_type)
{
    int retval = FALSE;
    if (old_type == new_type)
        return FALSE;

    if (i > 0        && LEFTOF_DOT(state, i, j) == old_type) {
        LV_LEFTOF_DOT(state, i, j) = new_type;
        retval = TRUE;
    }

    if (i < state->w && RIGHTOF_DOT(state, i, j) == old_type) {
        LV_RIGHTOF_DOT(state, i, j) = new_type;
        retval = TRUE;
    }

    if (j > 0        && ABOVE_DOT(state, i, j) == old_type) {
        LV_ABOVE_DOT(state, i, j) = new_type;
        retval = TRUE;
    }

    if (j < state->h && BELOW_DOT(state, i, j) == old_type) {
        LV_BELOW_DOT(state, i, j) = new_type;
        retval = TRUE;
    }

    return retval;
}
/* Set all lines bordering a square of type old_type to type new_type */
static void square_setall(game_state *state, int i, int j,
                          char old_type, char new_type)
{
    if (ABOVE_SQUARE(state, i, j) == old_type)
        ABOVE_SQUARE(state, i, j) = new_type;
    if (BELOW_SQUARE(state, i, j) == old_type)
        BELOW_SQUARE(state, i, j) = new_type;
    if (LEFTOF_SQUARE(state, i, j) == old_type)
        LEFTOF_SQUARE(state, i, j) = new_type;
    if (RIGHTOF_SQUARE(state, i, j) == old_type)
        RIGHTOF_SQUARE(state, i, j) = new_type;
}

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

#ifdef SLOW_SYSTEM
    ret->h = 4;
    ret->w = 4;
#else
    ret->h = 10;
    ret->w = 10;
#endif
    ret->diff = DIFF_EASY;
    ret->rec = 0;

    return ret;
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;                       /* structure copy */
    return ret;
}

static const struct {
    char *desc;
    game_params params;
} loopy_presets[] = {
    { "4x4 Easy",     {  4,  4, DIFF_EASY, 0 } },
    { "4x4 Normal",   {  4,  4, DIFF_NORMAL, 0 } },
    { "7x7 Easy",     {  7,  7, DIFF_EASY, 0 } },
    { "7x7 Normal",   {  7,  7, DIFF_NORMAL, 0 } },
    { "10x10 Easy",   { 10, 10, DIFF_EASY, 0 } },
    { "10x10 Normal", { 10, 10, DIFF_NORMAL, 0 } },
#ifndef SLOW_SYSTEM
    { "15x15 Easy",   { 15, 15, DIFF_EASY, 0 } },
    { "15x15 Normal", { 15, 15, DIFF_NORMAL, 0 } },
    { "30x20 Easy",   { 30, 20, DIFF_EASY, 0 } },
    { "30x20 Normal", { 30, 20, DIFF_NORMAL, 0 } }
#endif
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params tmppar;

    if (i < 0 || i >= lenof(loopy_presets))
        return FALSE;

    tmppar = loopy_presets[i].params;
    *params = dup_params(&tmppar);
    *name = dupstr(loopy_presets[i].desc);

    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static void decode_params(game_params *params, char const *string)
{
    params->h = params->w = atoi(string);
    params->rec = 0;
    params->diff = DIFF_EASY;
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'r') {
        string++;
        params->rec = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'd') {
        int i;

        string++;
	for (i = 0; i < DIFFCOUNT; i++)
	    if (*string == loopy_diffchars[i])
		params->diff = i;
	if (*string) string++;
    }
}

static char *encode_params(game_params *params, int full)
{
    char str[80];
    sprintf(str, "%dx%d", params->w, params->h);
    if (full)
	sprintf(str + strlen(str), "r%dd%c", params->rec,
                loopy_diffchars[params->diff]);
    return dupstr(str);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

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

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].sval = DIFFCONFIG;
    ret[2].ival = params->diff;

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
    ret->rec = 0;
    ret->diff = cfg[2].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->w < 4 || params->h < 4)
        return "Width and height must both be at least 4";
    if (params->rec < 0)
        return "Recursion depth can't be negative";

    /*
     * This shouldn't be able to happen at all, since decode_params
     * and custom_params will never generate anything that isn't
     * within range.
     */
    assert(params->diff >= 0 && params->diff < DIFFCOUNT);

    return NULL;
}

/* We're going to store a list of current candidate squares for lighting.
 * Each square gets a 'score', which tells us how adding that square right
 * now would affect the length of the solution loop.  We're trying to
 * maximise that quantity so will bias our random selection of squares to
 * light towards those with high scores */
struct square { 
    int score;
    unsigned long random;
    int x, y;
};

static int get_square_cmpfn(void *v1, void *v2) 
{
    struct square *s1 = (struct square *)v1;
    struct square *s2 = (struct square *)v2;
    int r;
    
    r = s1->x - s2->x;
    if (r)
        return r;

    r = s1->y - s2->y;
    if (r)
        return r;

    return 0;
}

static int square_sort_cmpfn(void *v1, void *v2)
{
    struct square *s1 = (struct square *)v1;
    struct square *s2 = (struct square *)v2;
    int r;

    r = s2->score - s1->score;
    if (r) {
        return r;
    }

    if (s1->random < s2->random)
        return -1;
    else if (s1->random > s2->random)
        return 1;

    /*
     * It's _just_ possible that two squares might have been given
     * the same random value. In that situation, fall back to
     * comparing based on the coordinates. This introduces a tiny
     * directional bias, but not a significant one.
     */
    return get_square_cmpfn(v1, v2);
}

static void print_tree(tree234 *tree)
{
#if 0
    int i = 0;
    struct square *s;
    printf("Print tree:\n");
    while (i < count234(tree)) {
        s = (struct square *)index234(tree, i);
        assert(s);
        printf("  [%d,%d], %d, %d\n", s->x, s->y, s->score, s->random);
        ++i;
    }
#endif
}

enum { SQUARE_LIT, SQUARE_UNLIT };

#define SQUARE_STATE(i, j)                 \
    (((i) < 0 || (i) >= params->w ||       \
      (j) < 0 || (j) >= params->h) ?       \
     SQUARE_UNLIT :  LV_SQUARE_STATE(i,j))

#define LV_SQUARE_STATE(i, j) board[(i) + params->w * (j)]

static void print_board(const game_params *params, const char *board)
{
#if 0
    int i,j;

    printf(" ");
    for (i = 0; i < params->w; i++) {
        printf("%d", i%10);
    }
    printf("\n");
    for (j = 0; j < params->h; j++) {
        printf("%d", j%10);
        for (i = 0; i < params->w; i++) {
            printf("%c", SQUARE_STATE(i, j) ? ' ' : 'O');
        }
        printf("\n");
    }
#endif
}

static char *new_fullyclued_board(game_params *params, random_state *rs)
{
    char *clues;
    char *board;
    int i, j, a, b, c;
    game_state s;
    game_state *state = &s;
    int board_area = SQUARE_COUNT(params);
    int t;

    struct square *square, *tmpsquare, *sq;
    struct square square_pos;

    /* These will contain exactly the same information, sorted into different
     * orders */
    tree234 *lightable_squares_sorted, *lightable_squares_gettable;

#define SQUARE_REACHABLE(i,j)                      \
     (t = (SQUARE_STATE(i-1, j) == SQUARE_LIT ||      \
           SQUARE_STATE(i+1, j) == SQUARE_LIT ||      \
           SQUARE_STATE(i, j-1) == SQUARE_LIT ||      \
           SQUARE_STATE(i, j+1) == SQUARE_LIT),       \
/*      printf("SQUARE_REACHABLE(%d,%d) = %d\n", i, j, t), */ \
      t)


    /* One situation in which we may not light a square is if that'll leave one
     * square above/below and one left/right of us unlit, separated by a lit
     * square diagnonal from us */
#define SQUARE_DIAGONAL_VIOLATION(i, j, h, v)           \
    (t = (SQUARE_STATE((i)+(h), (j))     == SQUARE_UNLIT && \
          SQUARE_STATE((i),     (j)+(v)) == SQUARE_UNLIT && \
          SQUARE_STATE((i)+(h), (j)+(v)) == SQUARE_LIT),    \
/*     t ? printf("SQUARE_DIAGONAL_VIOLATION(%d, %d, %d, %d)\n",
                  i, j, h, v) : 0,*/ \
     t)

    /* We also may not light a square if it will form a loop of lit squares
     * around some unlit squares, as then the game soln won't have a single
     * loop */
#define SQUARE_LOOP_VIOLATION(i, j, lit1, lit2) \
    (SQUARE_STATE((i)+1, (j)) == lit1    &&     \
     SQUARE_STATE((i)-1, (j)) == lit1    &&     \
     SQUARE_STATE((i), (j)+1) == lit2    &&     \
     SQUARE_STATE((i), (j)-1) == lit2)

#define CAN_LIGHT_SQUARE(i, j)                                 \
    (SQUARE_REACHABLE(i, j)                                 && \
     !SQUARE_DIAGONAL_VIOLATION(i, j, -1, -1)               && \
     !SQUARE_DIAGONAL_VIOLATION(i, j, +1, -1)               && \
     !SQUARE_DIAGONAL_VIOLATION(i, j, -1, +1)               && \
     !SQUARE_DIAGONAL_VIOLATION(i, j, +1, +1)               && \
     !SQUARE_LOOP_VIOLATION(i, j, SQUARE_LIT, SQUARE_UNLIT) && \
     !SQUARE_LOOP_VIOLATION(i, j, SQUARE_UNLIT, SQUARE_LIT))

#define IS_LIGHTING_CANDIDATE(i, j)        \
    (SQUARE_STATE(i, j) == SQUARE_UNLIT && \
     CAN_LIGHT_SQUARE(i,j))

    /* The 'score' of a square reflects its current desirability for selection
     * as the next square to light.  We want to encourage moving into uncharted
     * areas so we give scores according to how many of the square's neighbours
     * are currently unlit. */

   /* UNLIT    SCORE
    *   3        2
    *   2        0
    *   1       -2
    */
#define SQUARE_SCORE(i,j)                  \
    (2*((SQUARE_STATE(i-1, j) == SQUARE_UNLIT)  +   \
        (SQUARE_STATE(i+1, j) == SQUARE_UNLIT)  +   \
        (SQUARE_STATE(i, j-1) == SQUARE_UNLIT)  +   \
        (SQUARE_STATE(i, j+1) == SQUARE_UNLIT)) - 4)

    /* When a square gets lit, this defines how far away from that square we
     * need to go recomputing scores */
#define SCORE_DISTANCE 1

    board = snewn(board_area, char);
    clues = snewn(board_area, char);

    state->h = params->h;
    state->w = params->w;
    state->clues = clues;

    /* Make a board */
    memset(board, SQUARE_UNLIT, board_area);
    
    /* Seed the board with a single lit square near the middle */
    i = params->w / 2;
    j = params->h / 2;
    if (params->w & 1 && random_bits(rs, 1))
        ++i;
    if (params->h & 1 && random_bits(rs, 1))
        ++j;

    LV_SQUARE_STATE(i, j) = SQUARE_LIT;

    /* We need a way of favouring squares that will increase our loopiness.
     * We do this by maintaining a list of all candidate squares sorted by
     * their score and choose randomly from that with appropriate skew. 
     * In order to avoid consistently biasing towards particular squares, we
     * need the sort order _within_ each group of scores to be completely
     * random.  But it would be abusing the hospitality of the tree234 data
     * structure if our comparison function were nondeterministic :-).  So with
     * each square we associate a random number that does not change during a
     * particular run of the generator, and use that as a secondary sort key.
     * Yes, this means we will be biased towards particular random squares in
     * any one run but that doesn't actually matter. */
    
    lightable_squares_sorted   = newtree234(square_sort_cmpfn);
    lightable_squares_gettable = newtree234(get_square_cmpfn);
#define ADD_SQUARE(s)                                          \
    do {                                                       \
/*      printf("ADD SQUARE: [%d,%d], %d, %d\n",
               s->x, s->y, s->score, s->random);*/ \
        sq = add234(lightable_squares_sorted, s);              \
        assert(sq == s);                                       \
        sq = add234(lightable_squares_gettable, s);            \
        assert(sq == s);                                       \
    } while (0)

#define REMOVE_SQUARE(s)                                       \
    do {                                                       \
/*      printf("DELETE SQUARE: [%d,%d], %d, %d\n",
               s->x, s->y, s->score, s->random);*/ \
        sq = del234(lightable_squares_sorted, s);              \
        assert(sq);                                            \
        sq = del234(lightable_squares_gettable, s);            \
        assert(sq);                                            \
    } while (0)
        
#define HANDLE_DIR(a, b)                                       \
    square = snew(struct square);                              \
    square->x = (i)+(a);                                       \
    square->y = (j)+(b);                                       \
    square->score = 2;                                         \
    square->random = random_bits(rs, 31);                      \
    ADD_SQUARE(square);
    HANDLE_DIR(-1, 0);
    HANDLE_DIR( 1, 0);
    HANDLE_DIR( 0,-1);
    HANDLE_DIR( 0, 1);
#undef HANDLE_DIR
    
    /* Light squares one at a time until the board is interesting enough */
    while (TRUE)
    {
        /* We have count234(lightable_squares) possibilities, and in
         * lightable_squares_sorted they are sorted with the most desirable
         * first.  */
        c = count234(lightable_squares_sorted);
        if (c == 0)
            break;
        assert(c == count234(lightable_squares_gettable));

        /* Check that the best square available is any good */
        square = (struct square *)index234(lightable_squares_sorted, 0);
        assert(square);

	/*
	 * We never want to _decrease_ the loop's perimeter. Making
	 * moves that leave the perimeter the same is occasionally
	 * useful: if it were _never_ done then the user would be
	 * able to deduce illicitly that any degree-zero vertex was
	 * on the outside of the loop. So we do it sometimes but
	 * not always.
	 */
        if (square->score < 0 || (square->score == 0 &&
				  random_upto(rs, 2) == 0))
            break;

        print_tree(lightable_squares_sorted);
        assert(square->score == SQUARE_SCORE(square->x, square->y));
        assert(SQUARE_STATE(square->x, square->y) == SQUARE_UNLIT);
        assert(square->x >= 0 && square->x < params->w);
        assert(square->y >= 0 && square->y < params->h);
/*        printf("LIGHT SQUARE: [%d,%d], score = %d\n", square->x, square->y, square->score); */

        /* Update data structures */
        LV_SQUARE_STATE(square->x, square->y) = SQUARE_LIT;
        REMOVE_SQUARE(square);

        print_board(params, board);

        /* We might have changed the score of any squares up to 2 units away in
         * any direction */
        for (b = -SCORE_DISTANCE; b <= SCORE_DISTANCE; b++) {
            for (a = -SCORE_DISTANCE; a <= SCORE_DISTANCE; a++) {
                if (!a && !b) 
                    continue;
                square_pos.x = square->x + a;
                square_pos.y = square->y + b;
/*                printf("Refreshing score for [%d,%d]:\n", square_pos.x, square_pos.y); */
                if (square_pos.x < 0 || square_pos.x >= params->w ||
                    square_pos.y < 0 || square_pos.y >= params->h) {
/*                    printf("  Out of bounds\n"); */
                   continue; 
                }
                tmpsquare = find234(lightable_squares_gettable, &square_pos,
                                    NULL);
                if (tmpsquare) {
/*                    printf(" Removing\n"); */
                    assert(tmpsquare->x == square_pos.x);
                    assert(tmpsquare->y == square_pos.y);
                    assert(SQUARE_STATE(tmpsquare->x, tmpsquare->y) == 
                           SQUARE_UNLIT);
                    REMOVE_SQUARE(tmpsquare);
                } else {
/*                    printf(" Creating\n"); */
                    tmpsquare = snew(struct square);
                    tmpsquare->x = square_pos.x;
                    tmpsquare->y = square_pos.y;
                    tmpsquare->random = random_bits(rs, 31);
                }
                tmpsquare->score = SQUARE_SCORE(tmpsquare->x, tmpsquare->y);

                if (IS_LIGHTING_CANDIDATE(tmpsquare->x, tmpsquare->y)) {
/*                    printf(" Adding\n"); */
                    ADD_SQUARE(tmpsquare);
                } else {
/*                    printf(" Destroying\n"); */
                    sfree(tmpsquare);
                }
            }
        }
        sfree(square);
/*        printf("\n\n"); */
    }

    while ((square = delpos234(lightable_squares_gettable, 0)) != NULL)
        sfree(square);
    freetree234(lightable_squares_gettable);
    freetree234(lightable_squares_sorted);

    /* Copy out all the clues */
    for (j = 0; j < params->h; ++j) {
        for (i = 0; i < params->w; ++i) {
            c = SQUARE_STATE(i, j);
            LV_CLUE_AT(state, i, j) = '0';
            if (SQUARE_STATE(i-1, j) != c) ++LV_CLUE_AT(state, i, j);
            if (SQUARE_STATE(i+1, j) != c) ++LV_CLUE_AT(state, i, j);
            if (SQUARE_STATE(i, j-1) != c) ++LV_CLUE_AT(state, i, j);
            if (SQUARE_STATE(i, j+1) != c) ++LV_CLUE_AT(state, i, j);
        }
    }

    sfree(board);
    return clues;
}

static solver_state *solve_game_rec(const solver_state *sstate, int diff);

static int game_has_unique_soln(const game_state *state, int diff)
{
    int ret;
    solver_state *sstate_new;
    solver_state *sstate = new_solver_state((game_state *)state);
    
    sstate_new = solve_game_rec(sstate, diff);

    ret = (sstate_new->solver_status == SOLVER_SOLVED);

    free_solver_state(sstate_new);
    free_solver_state(sstate);

    return ret;
}

/* Remove clues one at a time at random. */
static game_state *remove_clues(game_state *state, random_state *rs, int diff)
{
    int *square_list, squares;
    game_state *ret = dup_game(state), *saved_ret;
    int n;

    /* We need to remove some clues.  We'll do this by forming a list of all
     * available equivalence classes, shuffling it, then going along one at a
     * time clearing every member of each equivalence class, where removing a
     * class doesn't render the board unsolvable. */
    squares = state->w * state->h;
    square_list = snewn(squares, int);
    for (n = 0; n < squares; ++n) {
        square_list[n] = n;
    }

    shuffle(square_list, squares, sizeof(int), rs);
    
    for (n = 0; n < squares; ++n) {
        saved_ret = dup_game(ret);
	LV_CLUE_AT(ret, square_list[n] % state->w,
		   square_list[n] / state->w) = ' ';
        if (game_has_unique_soln(ret, diff)) {
	    free_game(saved_ret);
        } else {
            free_game(ret);
            ret = saved_ret;
        }
    }
    sfree(square_list);

    return ret;
}

static char *validate_desc(game_params *params, char *desc);

static char *new_game_desc(game_params *params, random_state *rs,
                           char **aux, int interactive)
{
    /* solution and description both use run-length encoding in obvious ways */
    char *retval;
    char *description = snewn(SQUARE_COUNT(params) + 1, char);
    char *dp = description;
    int i, j;
    int empty_count;
    game_state *state = snew(game_state), *state_new;

    state->h = params->h;
    state->w = params->w;

    state->hl = snewn(HL_COUNT(params), char);
    state->vl = snewn(VL_COUNT(params), char);

newboard_please:
    memset(state->hl, LINE_UNKNOWN, HL_COUNT(params));
    memset(state->vl, LINE_UNKNOWN, VL_COUNT(params));

    state->solved = state->cheated = FALSE;
    state->recursion_depth = params->rec;

    /* Get a new random solvable board with all its clues filled in.  Yes, this
     * can loop for ever if the params are suitably unfavourable, but
     * preventing games smaller than 4x4 seems to stop this happening */

    do {
        state->clues = new_fullyclued_board(params, rs);
    } while (!game_has_unique_soln(state, params->diff));

    state_new = remove_clues(state, rs, params->diff);
    free_game(state);
    state = state_new;

    if (params->diff > 0 && game_has_unique_soln(state, params->diff-1)) {
        /* Board is too easy */
        goto newboard_please;
    }

    empty_count = 0;
    for (j = 0; j < params->h; ++j) {
        for (i = 0; i < params->w; ++i) {
            if (CLUE_AT(state, i, j) == ' ') {
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
                dp += sprintf(dp, "%c", (int)(CLUE_AT(state, i, j)));
            }
        }
    }
    if (empty_count)
        dp += sprintf(dp, "%c", (int)(empty_count + 'a' - 1));

    free_game(state);
    retval = dupstr(description);
    sfree(description);
    
    assert(!validate_desc(params, retval));

    return retval;
}

/* We require that the params pass the test in validate_params and that the
 * description fills the entire game area */
static char *validate_desc(game_params *params, char *desc)
{
    int count = 0;

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

    if (count < SQUARE_COUNT(params))
        return "Description too short for board size";
    if (count > SQUARE_COUNT(params))
        return "Description too long for board size";

    return NULL;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    int i,j;
    game_state *state = snew(game_state);
    int empties_to_make = 0;
    int n;
    const char *dp = desc;

    state->recursion_depth = 0; /* XXX pending removal, probably */
    
    state->h = params->h;
    state->w = params->w;

    state->clues = snewn(SQUARE_COUNT(params), char);
    state->hl    = snewn(HL_COUNT(params), char);
    state->vl    = snewn(VL_COUNT(params), char);

    state->solved = state->cheated = FALSE;

    for (j = 0 ; j < params->h; ++j) {
        for (i = 0 ; i < params->w; ++i) {
            if (empties_to_make) {
                empties_to_make--;
                LV_CLUE_AT(state, i, j) = ' ';
                continue;
            }

	    assert(*dp);
            n = *dp - '0';
            if (n >=0 && n < 10) {
                LV_CLUE_AT(state, i, j) = *dp;
            } else {
                n = *dp - 'a' + 1;
                assert(n > 0);
                LV_CLUE_AT(state, i, j) = ' ';
                empties_to_make = n - 1;
            }
            ++dp;
        }
    }

    memset(state->hl, LINE_UNKNOWN, HL_COUNT(params));
    memset(state->vl, LINE_UNKNOWN, VL_COUNT(params));

    return state;
}

enum { LOOP_NONE=0, LOOP_SOLN, LOOP_NOT_SOLN };

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
    int len, i, j;
    char *ret, *p;
    /* This is going to return a string representing the moves needed to set
     * every line in a grid to be the same as the ones in 'state'.  The exact
     * length of this string is predictable. */

    len = 1;  /* Count the 'S' prefix */
    /* Numbers in horizontal lines */
    /* Horizontal lines, x position */
    len += len_0_to_n(state->w) * (state->h + 1);
    /* Horizontal lines, y position */
    len += len_0_to_n(state->h + 1) * (state->w);
    /* Vertical lines, y position */
    len += len_0_to_n(state->h) * (state->w + 1);
    /* Vertical lines, x position */
    len += len_0_to_n(state->w + 1) * (state->h);
    /* For each line we also have two letters and a comma */
    len += 3 * (HL_COUNT(state) + VL_COUNT(state));

    ret = snewn(len + 1, char);
    p = ret;

    p += sprintf(p, "S");

    for (j = 0; j < state->h + 1; ++j) {
        for (i = 0; i < state->w; ++i) {
            switch (RIGHTOF_DOT(state, i, j)) {
                case LINE_YES:
                    p += sprintf(p, "%d,%dhy", i, j);
                    break;
                case LINE_NO:
                    p += sprintf(p, "%d,%dhn", i, j);
                    break;
/*                default: */
                    /* I'm going to forgive this because I think the results
                     * are cute. */
/*                    assert(!"Solver produced incomplete solution!"); */
            }
        }
    }

    for (j = 0; j < state->h; ++j) {
        for (i = 0; i < state->w + 1; ++i) {
            switch (BELOW_DOT(state, i, j)) {
                case LINE_YES:
                    p += sprintf(p, "%d,%dvy", i, j);
                    break;
                case LINE_NO:
                    p += sprintf(p, "%d,%dvn", i, j);
                    break;
/*                default: */
                    /* I'm going to forgive this because I think the results
                     * are cute. */
/*                    assert(!"Solver produced incomplete solution!"); */
            }
        }
    }

    /* No point in doing sums like that if they're going to be wrong */
    assert(strlen(ret) == (size_t)len);
    return ret;
}

/* BEGIN SOLVER IMPLEMENTATION */

   /* For each pair of lines through each dot we store a bit for whether
    * exactly one of those lines is ON, and in separate arrays we store whether
    * at least one is on and whether at most 1 is on.  (If we know both or
    * neither is on that's already stored more directly.)  That's six bits per
    * dot.  Bit number n represents the lines shown in dot_type_dirs[n]. */

enum dline {
    DLINE_VERT  = 0,
    DLINE_HORIZ = 1,
    DLINE_UL    = 2,
    DLINE_DR    = 3,
    DLINE_UR    = 4,
    DLINE_DL    = 5
};

#define OPP_DLINE(dline) (dline ^ 1)
   

#define SQUARE_DLINES                                                          \
                   HANDLE_DLINE(DLINE_UL, RIGHTOF_SQUARE, BELOW_SQUARE, 1, 1); \
                   HANDLE_DLINE(DLINE_UR, LEFTOF_SQUARE,  BELOW_SQUARE, 0, 1); \
                   HANDLE_DLINE(DLINE_DL, RIGHTOF_SQUARE, ABOVE_SQUARE, 1, 0); \
                   HANDLE_DLINE(DLINE_DR, LEFTOF_SQUARE,  ABOVE_SQUARE, 0, 0); 

#define DOT_DLINES                                                       \
                   HANDLE_DLINE(DLINE_VERT,  ABOVE_DOT,  BELOW_DOT);     \
                   HANDLE_DLINE(DLINE_HORIZ, LEFTOF_DOT, RIGHTOF_DOT);   \
                   HANDLE_DLINE(DLINE_UL,    ABOVE_DOT,  LEFTOF_DOT);    \
                   HANDLE_DLINE(DLINE_UR,    ABOVE_DOT,  RIGHTOF_DOT);   \
                   HANDLE_DLINE(DLINE_DL,    BELOW_DOT,  LEFTOF_DOT);    \
                   HANDLE_DLINE(DLINE_DR,    BELOW_DOT,  RIGHTOF_DOT); 

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

static int dot_setall_dlines(solver_state *sstate, enum dline dl, int i, int j,
                             enum line_state line_old, enum line_state line_new) 
{
    game_state *state = sstate->state;
    int retval = FALSE;

    if (line_old == line_new)
        return FALSE;

    /* First line in dline */
    switch (dl) {                                             
        case DLINE_UL:                                                  
        case DLINE_UR:                                                  
        case DLINE_VERT:                                                  
            if (j > 0 && ABOVE_DOT(state, i, j) == line_old) {
                LV_ABOVE_DOT(state, i, j) = line_new;                   
                retval = TRUE;
            }
            break;                                                          
        case DLINE_DL:                                                  
        case DLINE_DR:                                                  
            if (j <= (state)->h && BELOW_DOT(state, i, j) == line_old) {
                LV_BELOW_DOT(state, i, j) = line_new;                   
                retval = TRUE;
            }
            break;
        case DLINE_HORIZ:                                                  
            if (i > 0 && LEFTOF_DOT(state, i, j) == line_old) {
                LV_LEFTOF_DOT(state, i, j) = line_new;                  
                retval = TRUE;
            }
            break;                                                          
    }

    /* Second line in dline */
    switch (dl) {                                             
        case DLINE_UL:                                                  
        case DLINE_DL:                                                  
            if (i > 0 && LEFTOF_DOT(state, i, j) == line_old) {
                LV_LEFTOF_DOT(state, i, j) = line_new;                  
                retval = TRUE;
            }
            break;                                                          
        case DLINE_UR:                                                  
        case DLINE_DR:                                                  
        case DLINE_HORIZ:                                                  
            if (i <= (state)->w && RIGHTOF_DOT(state, i, j) == line_old) {
                LV_RIGHTOF_DOT(state, i, j) = line_new;                 
                retval = TRUE;
            }
            break;                                                          
        case DLINE_VERT:                                                  
            if (j <= (state)->h && BELOW_DOT(state, i, j) == line_old) {
                LV_BELOW_DOT(state, i, j) = line_new;                   
                retval = TRUE;
            }
            break;                                                          
    }

    return retval;
}

#if 0
/* This will fail an assertion if {dx,dy} are anything other than {-1,0}, {1,0}
 * {0,-1} or {0,1} */
static int line_status_from_point(const game_state *state,
                                  int x, int y, int dx, int dy)
{
    if (dx == -1 && dy ==  0)
        return LEFTOF_DOT(state, x, y);
    if (dx ==  1 && dy ==  0)
        return RIGHTOF_DOT(state, x, y);
    if (dx ==  0 && dy == -1)
        return ABOVE_DOT(state, x, y);
    if (dx ==  0 && dy ==  1)
        return BELOW_DOT(state, x, y);

    assert(!"Illegal dx or dy in line_status_from_point");
    return 0;
}
#endif

/* This will return a dynamically allocated solver_state containing the (more)
 * solved grid */
static solver_state *solve_game_rec(const solver_state *sstate_start, int diff)
{
   int i, j, w, h;
   int current_yes, current_no, desired;
   solver_state *sstate, *sstate_saved, *sstate_tmp;
   int t;
   solver_state *sstate_rec_solved;
   int recursive_soln_count;
   char *square_solved;
   char *dot_solved;
   int solver_progress;

   h = sstate_start->state->h;
   w = sstate_start->state->w;

   dot_solved = snewn(DOT_COUNT(sstate_start->state), char);
   square_solved = snewn(SQUARE_COUNT(sstate_start->state), char);
   memset(dot_solved, FALSE, DOT_COUNT(sstate_start->state));
   memset(square_solved, FALSE, SQUARE_COUNT(sstate_start->state));

#if 0
   printf("solve_game_rec: recursion_remaining = %d\n", 
          sstate_start->recursion_remaining);
#endif

   sstate = dup_solver_state((solver_state *)sstate_start);

#define FOUND_MISTAKE                                    \
   do {                                                  \
       sstate->solver_status = SOLVER_MISTAKE;           \
       sfree(dot_solved);  sfree(square_solved);         \
       free_solver_state(sstate_saved);                  \
       return sstate;                                    \
   } while (0)

   sstate_saved = NULL;

nonrecursive_solver:
   
   while (1) {
       solver_progress = FALSE;

       /* First we do the 'easy' work, that might cause concrete results */

       /* Per-square deductions */
       for (j = 0; j < h; ++j) {
           for (i = 0; i < w; ++i) {
               /* Begin rules that look at the clue (if there is one) */
               if (square_solved[i + j*w])
                   continue;

               desired = CLUE_AT(sstate->state, i, j);
               if (desired == ' ')
                   continue;

               desired = desired - '0';
               current_yes = square_order(sstate->state, i, j, LINE_YES);
               current_no  = square_order(sstate->state, i, j, LINE_NO);

               if (current_yes + current_no == 4)  {
                   square_solved[i + j*w] = TRUE;
                   continue;
               }

               if (desired < current_yes) 
                   FOUND_MISTAKE;
               if (desired == current_yes) {
                   square_setall(sstate->state, i, j, LINE_UNKNOWN, LINE_NO);
                   square_solved[i + j*w] = TRUE;
                   solver_progress = TRUE;
                   continue;
               }

               if (4 - desired < current_no) 
                   FOUND_MISTAKE;
               if (4 - desired == current_no) {
                   square_setall(sstate->state, i, j, LINE_UNKNOWN, LINE_YES);
                   square_solved[i + j*w] = TRUE;
                   solver_progress = TRUE;
               }
           }
       }

       /* Per-dot deductions */
       for (j = 0; j < h + 1; ++j) {
           for (i = 0; i < w + 1; ++i) {
               if (dot_solved[i + j*(w+1)])
                   continue;

               switch (dot_order(sstate->state, i, j, LINE_YES)) {
               case 0:
                   switch (dot_order(sstate->state, i, j, LINE_NO)) {
                       case 3:
                           dot_setall(sstate->state, i, j, LINE_UNKNOWN, LINE_NO);
                           solver_progress = TRUE;
                           /* fall through */
                       case 4:
                           dot_solved[i + j*(w+1)] = TRUE;
                           break;
                   }
                   break;
               case 1:
                   switch (dot_order(sstate->state, i, j, LINE_NO)) {
#define H1(dline, dir1_dot, dir2_dot, dot_howmany)                             \
                       if (dir1_dot(sstate->state, i, j) == LINE_UNKNOWN) {    \
                           if (dir2_dot(sstate->state, i, j) == LINE_UNKNOWN){ \
                               solver_progress |=                              \
                                 SET_BIT(sstate->dot_howmany[i + (w + 1) * j], \
                                         dline);                               \
                           }                                                   \
                       }
                       case 1: 
                           if (diff > DIFF_EASY) {
#define HANDLE_DLINE(dline, dir1_dot, dir2_dot)                               \
                           H1(dline, dir1_dot, dir2_dot, dot_atleastone)
                               /* 1 yes, 1 no, so exactly one of unknowns is
                                * yes */
                               DOT_DLINES;
#undef HANDLE_DLINE
                           }
                           /* fall through */
                       case 0: 
                           if (diff > DIFF_EASY) {
#define HANDLE_DLINE(dline, dir1_dot, dir2_dot)                               \
                           H1(dline, dir1_dot, dir2_dot, dot_atmostone)
                               /* 1 yes, fewer than 2 no, so at most one of
                                * unknowns is yes */
                               DOT_DLINES;
#undef HANDLE_DLINE
                           }
#undef H1
                           break;
                       case 2: /* 1 yes, 2 no */
                           dot_setall(sstate->state, i, j, 
                                      LINE_UNKNOWN, LINE_YES);
                           dot_solved[i + j*(w+1)] = TRUE;
                           solver_progress = TRUE;
                           break;
                       case 3: /* 1 yes, 3 no */
                           FOUND_MISTAKE;
                           break;
                   }
                   break;
               case 2:
                   if (dot_setall(sstate->state, i, j, LINE_UNKNOWN, LINE_NO)) {
                       solver_progress = TRUE;
                   }
                   dot_solved[i + j*(w+1)] = TRUE;
                   break;
               case 3:
               case 4:
                   FOUND_MISTAKE;
                   break;
               }
               if (diff > DIFF_EASY) {
#define HANDLE_DLINE(dline, dir1_dot, dir2_dot)                               \
               if (BIT_SET(sstate->dot_atleastone[i + (w + 1) * j], dline)) { \
                   solver_progress |=                                         \
                     SET_BIT(sstate->dot_atmostone[i + (w + 1) * j],          \
                             OPP_DLINE(dline));                               \
               }
                   /* If at least one of a dline in a dot is YES, at most one
                    * of the opposite dline to that dot must be YES. */
                   DOT_DLINES;
               }
#undef HANDLE_DLINE

#define H1(dline, dir1_sq, dir2_sq, dot_howmany, line_query, line_set)        \
               if (BIT_SET(sstate->dot_howmany[i + (w+1) * j], dline)) {      \
                   t = dir1_sq(sstate->state, i, j);                          \
                   if (t == line_query) {                                     \
                       if (dir2_sq(sstate->state, i, j) != line_set) {        \
                           LV_##dir2_sq(sstate->state, i, j) = line_set;      \
                           solver_progress = TRUE;                            \
                       }                                                      \
                   } else {                                                   \
                       t = dir2_sq(sstate->state, i, j);                      \
                       if (t == line_query) {                                 \
                           if (dir1_sq(sstate->state, i, j) != line_set) {    \
                               LV_##dir1_sq(sstate->state, i, j) = line_set;  \
                               solver_progress = TRUE;                        \
                           }                                                  \
                       }                                                      \
                   }                                                          \
               }
               if (diff > DIFF_EASY) {
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq)                                 \
               H1(dline, dir1_sq, dir2_sq, dot_atmostone, LINE_YES, LINE_NO)
                   /* If at most one of the DLINE is on, and one is definitely
                    * on, set the other to definitely off */
                   DOT_DLINES;
#undef HANDLE_DLINE
               }

               if (diff > DIFF_EASY) {
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq)                                 \
               H1(dline, dir1_sq, dir2_sq, dot_atleastone, LINE_NO, LINE_YES)
                   /* If at least one of the DLINE is on, and one is definitely
                    * off, set the other to definitely on */
                   DOT_DLINES;
#undef HANDLE_DLINE
               }
#undef H1

           }
       }

       /* More obscure per-square operations */
       for (j = 0; j < h; ++j) {
           for (i = 0; i < w; ++i) {
               if (square_solved[i + j*w])
                   continue;

               switch (CLUE_AT(sstate->state, i, j)) {
                   case '1':
                       if (diff > DIFF_EASY) {
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq, a, b)                          \
                       /* At most one of any DLINE can be set */             \
                       SET_BIT(sstate->dot_atmostone[i+a + (w + 1) * (j+b)], \
                               dline);                                       \
                       /* This DLINE provides enough YESes to solve the clue */\
                       if (BIT_SET(sstate->dot_atleastone                    \
                                      [i+a + (w + 1) * (j+b)],               \
                                   dline)) {                                 \
                           solver_progress |=                                \
                               dot_setall_dlines(sstate, OPP_DLINE(dline),   \
                                                 i+(1-a), j+(1-b),           \
                                                 LINE_UNKNOWN, LINE_NO);     \
                       }
                           SQUARE_DLINES;
#undef HANDLE_DLINE
                       }
                       break;
                   case '2':
                       if (diff > DIFF_EASY) {
#define H1(dline, dot_at1one, dot_at2one, a, b)                          \
               if (BIT_SET(sstate->dot_at1one                            \
                             [i+a + (w+1) * (j+b)], dline)) {            \
                   solver_progress |=                                    \
                     SET_BIT(sstate->dot_at2one                          \
                               [i+(1-a) + (w+1) * (j+(1-b))],            \
                             OPP_DLINE(dline));                          \
               }
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq, a, b)             \
            H1(dline, dot_atleastone, dot_atmostone, a, b);     \
            H1(dline, dot_atmostone, dot_atleastone, a, b); 
                           /* If at least one of one DLINE is set, at most one
                            * of the opposing one is and vice versa */
                           SQUARE_DLINES;
                       }
#undef HANDLE_DLINE
#undef H1
                       break;
                   case '3':
                       if (diff > DIFF_EASY) {
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq, a, b)                           \
                       /* At least one of any DLINE can be set */             \
                       solver_progress |=                                     \
                           SET_BIT(sstate->dot_atleastone                     \
                                     [i+a + (w + 1) * (j+b)],                 \
                                   dline);                                    \
                       /* This DLINE provides enough NOs to solve the clue */ \
                       if (BIT_SET(sstate->dot_atmostone                      \
                                     [i+a + (w + 1) * (j+b)],                 \
                                   dline)) {                                  \
                           solver_progress |=                                 \
                               dot_setall_dlines(sstate, OPP_DLINE(dline),    \
                                                 i+(1-a), j+(1-b),            \
                                                 LINE_UNKNOWN, LINE_YES);     \
                       }
                           SQUARE_DLINES;
#undef HANDLE_DLINE
                       }
                       break;
               }
           }
       }
       
       if (!solver_progress) {
	   int edgecount = 0, clues = 0, satclues = 0, sm1clues = 0;
           int shortest_chainlen = DOT_COUNT(sstate->state);
           int loop_found = FALSE;
	   int d;
           int dots_connected;

	   /*
	    * Go through the grid and update for all the new edges.
	    * Since merge_dots() is idempotent, the simplest way to
	    * do this is just to update for _all_ the edges.
	    * 
	    * Also, while we're here, we count the edges, count the
	    * clues, count the satisfied clues, and count the
	    * satisfied-minus-one clues.
	    */
	   for (j = 0; j < h+1; ++j) {
	       for (i = 0; i < w+1; ++i) {
		   if (RIGHTOF_DOT(sstate->state, i, j) == LINE_YES) {
		       loop_found |= merge_dots(sstate, i, j, i+1, j);
		       edgecount++;
		   }
		   if (BELOW_DOT(sstate->state, i, j) == LINE_YES) {
		       loop_found |= merge_dots(sstate, i, j, i, j+1);
		       edgecount++;
		   }

		   if (CLUE_AT(sstate->state, i, j) != ' ') {
		       int c = CLUE_AT(sstate->state, i, j) - '0';
		       int o = square_order(sstate->state, i, j, LINE_YES);
		       if (o == c)
			   satclues++;
		       else if (o == c-1)
			   sm1clues++;
		       clues++;
		   }
	       }
	   }

           for (i = 0; i < DOT_COUNT(sstate->state); ++i) {
               dots_connected = sstate->looplen[dsf_canonify(sstate->dotdsf,i)];
               if (dots_connected > 1)
                   shortest_chainlen = min(shortest_chainlen, dots_connected);
           }

           assert(sstate->solver_status == SOLVER_INCOMPLETE);

           if (satclues == clues && shortest_chainlen == edgecount) {
               sstate->solver_status = SOLVER_SOLVED;
               /* This discovery clearly counts as progress, even if we haven't
                * just added any lines or anything */
               solver_progress = TRUE; 
               goto finished_loop_checking;
           }

	   /*
	    * Now go through looking for LINE_UNKNOWN edges which
	    * connect two dots that are already in the same
	    * equivalence class. If we find one, test to see if the
	    * loop it would create is a solution.
	    */
	   for (j = 0; j <= h; ++j) {
	       for (i = 0; i <= w; ++i) {
		   for (d = 0; d < 2; d++) {
		       int i2, j2, eqclass, val;

		       if (d == 0) {
			   if (RIGHTOF_DOT(sstate->state, i, j) !=
			       LINE_UNKNOWN)
			       continue;
			   i2 = i+1;
			   j2 = j;
		       } else {
			   if (BELOW_DOT(sstate->state, i, j) !=
			       LINE_UNKNOWN)
			       continue;
			   i2 = i;
			   j2 = j+1;
		       }

		       eqclass = dsf_canonify(sstate->dotdsf, j * (w+1) + i);
		       if (eqclass != dsf_canonify(sstate->dotdsf,
						   j2 * (w+1) + i2))
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
			   int cx, cy;

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
			   cx = i - (j2-j);
			   cy = j - (i2-i);
			   if (CLUE_AT(sstate->state, cx,cy) != ' ' &&
			       square_order(sstate->state, cx,cy, LINE_YES) ==
			       CLUE_AT(sstate->state, cx,cy) - '0' - 1)
			       sm1_nearby++;
			   if (CLUE_AT(sstate->state, i, j) != ' ' &&
			       square_order(sstate->state, i, j, LINE_YES) ==
			       CLUE_AT(sstate->state, i, j) - '0' - 1)
			       sm1_nearby++;
			   if (sm1clues == sm1_nearby &&
			       sm1clues + satclues == clues)
			       val = LINE_YES;  /* loop is good! */
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
		       if (d == 0) {
			   LV_RIGHTOF_DOT(sstate->state, i, j) = val;
                           solver_progress = TRUE;
                       } else {
			   LV_BELOW_DOT(sstate->state, i, j) = val;
                           solver_progress = TRUE;
                       }
		       if (val == LINE_YES) {
                           sstate->solver_status = SOLVER_AMBIGUOUS;
			   goto finished_loop_checking;
		       }
		   }
	       }
	   }

	   finished_loop_checking:

           if (!solver_progress || 
               sstate->solver_status == SOLVER_SOLVED || 
               sstate->solver_status == SOLVER_AMBIGUOUS) {
               break;
           }
       }
   }

   sfree(dot_solved); sfree(square_solved);

   if (sstate->solver_status == SOLVER_SOLVED ||
       sstate->solver_status == SOLVER_AMBIGUOUS) {
       /* s/LINE_UNKNOWN/LINE_NO/g */
       array_setall(sstate->state->hl, LINE_UNKNOWN, LINE_NO, 
               HL_COUNT(sstate->state));
       array_setall(sstate->state->vl, LINE_UNKNOWN, LINE_NO, 
               VL_COUNT(sstate->state));
       return sstate;
   }

   /* Perform recursive calls */
   if (sstate->recursion_remaining) {
       sstate_saved = dup_solver_state(sstate);

       sstate->recursion_remaining--;

       recursive_soln_count = 0;
       sstate_rec_solved = NULL;

       /* Memory management: 
        * sstate_saved won't be modified but needs to be freed when we have
        * finished with it.
        * sstate is expected to contain our 'best' solution by the time we
        * finish this section of code.  It's the thing we'll try adding lines
        * to, seeing if they make it more solvable.
        * If sstate_rec_solved is non-NULL, it will supersede sstate
        * eventually.  sstate_tmp should not hold a value persistently.
        */

       /* NB SOLVER_AMBIGUOUS is like SOLVER_SOLVED except the solver is aware
        * of the possibility of additional solutions.  So as soon as we have a
        * SOLVER_AMBIGUOUS we can safely propagate it back to our caller, but
        * if we get a SOLVER_SOLVED we want to keep trying in case we find
        * further solutions and have to mark it ambiguous.
        */

#define DO_RECURSIVE_CALL(dir_dot)                                         \
       if (dir_dot(sstate->state, i, j) == LINE_UNKNOWN) {                 \
           debug(("Trying " #dir_dot " at [%d,%d]\n", i, j));               \
           LV_##dir_dot(sstate->state, i, j) = LINE_YES;                   \
           sstate_tmp = solve_game_rec(sstate, diff);                      \
           switch (sstate_tmp->solver_status) {                            \
               case SOLVER_AMBIGUOUS:                                      \
                   debug(("Solver ambiguous, returning\n"));                \
                   sstate_rec_solved = sstate_tmp;                         \
                   goto finished_recursion;                                \
               case SOLVER_SOLVED:                                         \
                   switch (++recursive_soln_count) {                       \
                       case 1:                                             \
                           debug(("One solution found\n"));                 \
                           sstate_rec_solved = sstate_tmp;                 \
                           break;                                          \
                       case 2:                                             \
                           debug(("Ambiguous solutions found\n"));          \
                           free_solver_state(sstate_tmp);                  \
                           sstate_rec_solved->solver_status = SOLVER_AMBIGUOUS;\
                           goto finished_recursion;                        \
                       default:                                            \
                           assert(!"recursive_soln_count out of range");   \
                           break;                                          \
                   }                                                       \
                   break;                                                  \
               case SOLVER_MISTAKE:                                        \
                   debug(("Non-solution found\n"));                         \
                   free_solver_state(sstate_tmp);                          \
                   free_solver_state(sstate_saved);                        \
                   LV_##dir_dot(sstate->state, i, j) = LINE_NO;            \
                   goto nonrecursive_solver;                               \
               case SOLVER_INCOMPLETE:                                     \
                   debug(("Recursive step inconclusive\n"));                \
                   free_solver_state(sstate_tmp);                          \
                   break;                                                  \
           }                                                               \
           free_solver_state(sstate);                                      \
           sstate = dup_solver_state(sstate_saved);                        \
       }
       
       for (j = 0; j < h + 1; ++j) {
           for (i = 0; i < w + 1; ++i) {
               /* Only perform recursive calls on 'loose ends' */
               if (dot_order(sstate->state, i, j, LINE_YES) == 1) {
                   DO_RECURSIVE_CALL(LEFTOF_DOT);
                   DO_RECURSIVE_CALL(RIGHTOF_DOT);
                   DO_RECURSIVE_CALL(ABOVE_DOT);
                   DO_RECURSIVE_CALL(BELOW_DOT);
               }
           }
       }

finished_recursion:

       if (sstate_rec_solved) {
           free_solver_state(sstate);
           sstate = sstate_rec_solved;
       } 
   }

   return sstate;
}

/* XXX bits of solver that may come in handy one day */
#if 0
#define HANDLE_DLINE(dline, dir1_dot, dir2_dot)                         \
                   /* dline from this dot that's entirely unknown must have 
                    * both lines identical */                           \
                   if (dir1_dot(sstate->state, i, j) == LINE_UNKNOWN &&       \
                       dir2_dot(sstate->state, i, j) == LINE_UNKNOWN) {       \
                       sstate->dline_identical[i + (sstate->state->w + 1) * j] |= \
                           1<<dline;                                    \
                   } else if (sstate->dline_identical[i +
                                                      (sstate->state->w + 1) * j] &\
                              1<<dline) {                                   \
                       /* If they're identical and one is known do the obvious 
                        * thing */                                      \
                       t = dir1_dot(sstate->state, i, j);                     \
                       if (t != LINE_UNKNOWN)                           \
                           dir2_dot(sstate->state, i, j) = t;                 \
                       else {                                           \
                           t = dir2_dot(sstate->state, i, j);                 \
                           if (t != LINE_UNKNOWN)                       \
                               dir1_dot(sstate->state, i, j) = t;             \
                       }                                                \
                   }                                                    \
                   DOT_DLINES;
#undef HANDLE_DLINE
#endif

#if 0
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq, a, b) \
                       if (sstate->dline_identical[i+a +                     \
                                                   (sstate->state->w + 1) * (j+b)] &\
                           1<<dline) {                                       \
                           dir1_sq(sstate->state, i, j) = LINE_YES;                \
                           dir2_sq(sstate->state, i, j) = LINE_YES;                \
                       }
                       /* If two lines are the same they must be on */
                       SQUARE_DLINES;
#undef HANDLE_DLINE
#endif


#if 0
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq, a, b) \
               if (sstate->dot_atmostone[i+a + (sstate->state->w + 1) * (j+b)] &  \
                   1<<dline) {                                   \
                   if (square_order(sstate->state, i, j,  LINE_UNKNOWN) - 1 ==  \
                       CLUE_AT(sstate->state, i, j) - '0') {      \
                       square_setall(sstate->state, i, j, LINE_UNKNOWN, LINE_YES); \
                           /* XXX the following may overwrite known data! */ \
                       dir1_sq(sstate->state, i, j) = LINE_UNKNOWN;  \
                       dir2_sq(sstate->state, i, j) = LINE_UNKNOWN;  \
                   }                                  \
               }
               SQUARE_DLINES;
#undef HANDLE_DLINE
#endif

#if 0
#define HANDLE_DLINE(dline, dir1_sq, dir2_sq, a, b) \
                       if (sstate->dline_identical[i+a + 
                                                   (sstate->state->w + 1) * (j+b)] &\
                           1<<dline) {                                       \
                           dir1_sq(sstate->state, i, j) = LINE_NO;                 \
                           dir2_sq(sstate->state, i, j) = LINE_NO;                 \
                       }
                       /* If two lines are the same they must be off */
                       SQUARE_DLINES;
#undef HANDLE_DLINE
#endif

static char *solve_game(game_state *state, game_state *currstate,
                        char *aux, char **error)
{
    char *soln = NULL;
    solver_state *sstate, *new_sstate;

    sstate = new_solver_state(state);
    new_sstate = solve_game_rec(sstate, DIFFCOUNT);

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

static char *game_text_format(game_state *state)
{
    int i, j;
    int len;
    char *ret, *rp;

    len = (2 * state->w + 2) * (2 * state->h + 1);
    rp = ret = snewn(len + 1, char);
    
#define DRAW_HL                          \
    switch (ABOVE_SQUARE(state, i, j)) { \
        case LINE_YES:                   \
            rp += sprintf(rp, " -");     \
            break;                       \
        case LINE_NO:                    \
            rp += sprintf(rp, " x");     \
            break;                       \
        case LINE_UNKNOWN:               \
            rp += sprintf(rp, "  ");     \
            break;                       \
        default:                         \
            assert(!"Illegal line state for HL");\
    }

#define DRAW_VL                          \
    switch (LEFTOF_SQUARE(state, i, j)) {\
        case LINE_YES:                   \
            rp += sprintf(rp, "|");      \
            break;                       \
        case LINE_NO:                    \
            rp += sprintf(rp, "x");      \
            break;                       \
        case LINE_UNKNOWN:               \
            rp += sprintf(rp, " ");      \
            break;                       \
        default:                         \
            assert(!"Illegal line state for VL");\
    }
    
    for (j = 0; j < state->h; ++j) {
        for (i = 0; i < state->w; ++i) {
            DRAW_HL;
        }
        rp += sprintf(rp, " \n");
        for (i = 0; i < state->w; ++i) {
            DRAW_VL;
            rp += sprintf(rp, "%c", (int)(CLUE_AT(state, i, j)));
        }
        DRAW_VL;
        rp += sprintf(rp, "\n");
    }
    for (i = 0; i < state->w; ++i) {
        DRAW_HL;
    }
    rp += sprintf(rp, " \n");
    
    assert(strlen(ret) == len);
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

struct game_drawstate {
    int started;
    int tilesize, linewidth;
    int flashing;
    char *hl, *vl;
    char *clue_error;
};

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
                            int x, int y, int button)
{
    int hl_selected;
    int i, j, p, q; 
    char *ret, buf[80];
    char button_char = ' ';
    enum line_state old_state;

    button &= ~MOD_MASK;

    /* Around each line is a diamond-shaped region where points within that
     * region are closer to this line than any other.  We assume any click
     * within a line's diamond was meant for that line.  It would all be a lot
     * simpler if the / and % operators respected modulo arithmetic properly
     * for negative numbers. */
    
    x -= BORDER;
    y -= BORDER;

    /* Get the coordinates of the square the click was in */
    i = (x + TILE_SIZE) / TILE_SIZE - 1; 
    j = (y + TILE_SIZE) / TILE_SIZE - 1;

    /* Get the precise position inside square [i,j] */
    p = (x + TILE_SIZE) % TILE_SIZE;
    q = (y + TILE_SIZE) % TILE_SIZE;

    /* After this bit of magic [i,j] will correspond to the point either above
     * or to the left of the line selected */
    if (p > q) { 
        if (TILE_SIZE - p > q) {
            hl_selected = TRUE;
        } else {
            hl_selected = FALSE;
            ++i;
        }
    } else {
        if (TILE_SIZE - q > p) {
            hl_selected = FALSE;
        } else {
            hl_selected = TRUE;
            ++j;
        }
    }

    if (i < 0 || j < 0)
        return NULL;

    if (hl_selected) {
        if (i >= state->w || j >= state->h + 1)
            return NULL;
    } else { 
        if (i >= state->w + 1 || j >= state->h)
            return NULL;
    }

    /* I think it's only possible to play this game with mouse clicks, sorry */
    /* Maybe will add mouse drag support some time */
    if (hl_selected)
        old_state = RIGHTOF_DOT(state, i, j);
    else
        old_state = BELOW_DOT(state, i, j);

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


    sprintf(buf, "%d,%d%c%c", i, j, (int)(hl_selected ? 'h' : 'v'), (int)button_char);
    ret = dupstr(buf);

    return ret;
}

static game_state *execute_move(game_state *state, char *move)
{
    int i, j;
    game_state *newstate = dup_game(state);

    if (move[0] == 'S') {
        move++;
        newstate->cheated = TRUE;
    }

    while (*move) {
        i = atoi(move);
        move = strchr(move, ',');
        if (!move)
            goto fail;
        j = atoi(++move);
        move += strspn(move, "1234567890");
        switch (*(move++)) {
            case 'h':
                if (i >= newstate->w || j > newstate->h)
                    goto fail;
                switch (*(move++)) {
                    case 'y':
                        LV_RIGHTOF_DOT(newstate, i, j) = LINE_YES;
                        break;
                    case 'n':
                        LV_RIGHTOF_DOT(newstate, i, j) = LINE_NO;
                        break;
                    case 'u':
                        LV_RIGHTOF_DOT(newstate, i, j) = LINE_UNKNOWN;
                        break;
                    default:
                        goto fail;
                }
                break;
            case 'v':
                if (i > newstate->w || j >= newstate->h)
                    goto fail;
                switch (*(move++)) {
                    case 'y':
                        LV_BELOW_DOT(newstate, i, j) = LINE_YES;
                        break;
                    case 'n':
                        LV_BELOW_DOT(newstate, i, j) = LINE_NO;
                        break;
                    case 'u':
                        LV_BELOW_DOT(newstate, i, j) = LINE_UNKNOWN;
                        break;
                    default:
                        goto fail;
                }
                break;
            default:
                goto fail;
        }
    }

    /*
     * Check for completion.
     */
    i = 0;			       /* placate optimiser */
    for (j = 0; j <= newstate->h; j++) {
	for (i = 0; i < newstate->w; i++)
	    if (LV_RIGHTOF_DOT(newstate, i, j) == LINE_YES)
		break;
	if (i < newstate->w)
	    break;
    }
    if (j <= newstate->h) {
	int prevdir = 'R';
	int x = i, y = j;
	int looplen, count;

	/*
	 * We've found a horizontal edge at (i,j). Follow it round
	 * to see if it's part of a loop.
	 */
	looplen = 0;
	while (1) {
	    int order = dot_order(newstate, x, y, LINE_YES);
	    if (order != 2)
		goto completion_check_done;

	    if (LEFTOF_DOT(newstate, x, y) == LINE_YES && prevdir != 'L') {
		x--;
		prevdir = 'R';
	    } else if (RIGHTOF_DOT(newstate, x, y) == LINE_YES &&
		       prevdir != 'R') {
		x++;
		prevdir = 'L';
	    } else if (ABOVE_DOT(newstate, x, y) == LINE_YES &&
		       prevdir != 'U') {
		y--;
		prevdir = 'D';
	    } else if (BELOW_DOT(newstate, x, y) == LINE_YES &&
		       prevdir != 'D') {
		y++;
		prevdir = 'U';
	    } else {
		assert(!"Can't happen");   /* dot_order guarantees success */
	    }

	    looplen++;

	    if (x == i && y == j)
		break;
	}

	if (x != i || y != j || looplen == 0)
	    goto completion_check_done;

	/*
	 * We've traced our way round a loop, and we know how many
	 * line segments were involved. Count _all_ the line
	 * segments in the grid, to see if the loop includes them
	 * all.
	 */
	count = 0;
	for (j = 0; j <= newstate->h; j++)
	    for (i = 0; i <= newstate->w; i++)
		count += ((RIGHTOF_DOT(newstate, i, j) == LINE_YES) +
			  (BELOW_DOT(newstate, i, j) == LINE_YES));
	assert(count >= looplen);
	if (count != looplen)
	    goto completion_check_done;

	/*
	 * The grid contains one closed loop and nothing else.
	 * Check that all the clues are satisfied.
	 */
	for (j = 0; j < newstate->h; ++j) {
	    for (i = 0; i < newstate->w; ++i) {
		int n = CLUE_AT(newstate, i, j);
		if (n != ' ') {
		    if (square_order(newstate, i, j, LINE_YES) != n - '0') {
			goto completion_check_done;
		    }
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

#define SIZE(d) ((d) * TILE_SIZE + 2 * BORDER + 1)

static void game_compute_size(game_params *params, int tilesize,
                              int *x, int *y)
{
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = SIZE(params->w);
    *y = SIZE(params->h);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    ds->linewidth = max(1,tilesize/16);
}

static float *game_colours(frontend *fe, game_state *state, int *ncolours)
{
    float *ret = snewn(4 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_FOREGROUND * 3 + 0] = 0.0F;
    ret[COL_FOREGROUND * 3 + 1] = 0.0F;
    ret[COL_FOREGROUND * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 1] = 1.0F;
    ret[COL_HIGHLIGHT * 3 + 2] = 1.0F;

    ret[COL_MISTAKE * 3 + 0] = 1.0F;
    ret[COL_MISTAKE * 3 + 1] = 0.0F;
    ret[COL_MISTAKE * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = ds->linewidth = 0;
    ds->started = 0;
    ds->hl = snewn(HL_COUNT(state), char);
    ds->vl = snewn(VL_COUNT(state), char);
    ds->clue_error = snewn(SQUARE_COUNT(state), char);
    ds->flashing = 0;

    memset(ds->hl, LINE_UNKNOWN, HL_COUNT(state));
    memset(ds->vl, LINE_UNKNOWN, VL_COUNT(state));
    memset(ds->clue_error, 0, SQUARE_COUNT(state));

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->clue_error);
    sfree(ds->hl);
    sfree(ds->vl);
    sfree(ds);
}

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
                        game_state *state, int dir, game_ui *ui,
                        float animtime, float flashtime)
{
    int i, j, n;
    int w = state->w, h = state->h;
    char c[2];
    int line_colour, flash_changed;
    int clue_mistake;

    if (!ds->started) {
        /*
         * The initial contents of the window are not guaranteed and
         * can vary with front ends. To be on the safe side, all games
         * should start by drawing a big background-colour rectangle
         * covering the whole window.
         */
        draw_rect(dr, 0, 0, SIZE(state->w), SIZE(state->h), COL_BACKGROUND);

        /* Draw dots */
        for (j = 0; j < h + 1; ++j) {
            for (i = 0; i < w + 1; ++i) {
                draw_rect(dr, 
                          BORDER + i * TILE_SIZE - LINEWIDTH/2,
                          BORDER + j * TILE_SIZE - LINEWIDTH/2,
                          LINEWIDTH, LINEWIDTH, COL_FOREGROUND);
            }
        }

        /* Draw clues */
        for (j = 0; j < h; ++j) {
            for (i = 0; i < w; ++i) {
                c[0] = CLUE_AT(state, i, j);
                c[1] = '\0';
                draw_text(dr, 
                          BORDER + i * TILE_SIZE + TILE_SIZE/2,
                          BORDER + j * TILE_SIZE + TILE_SIZE/2,
                          FONT_VARIABLE, TILE_SIZE/2, 
                          ALIGN_VCENTRE | ALIGN_HCENTRE, COL_FOREGROUND, c);
            }
        }
        draw_update(dr, 0, 0,
                    state->w * TILE_SIZE + 2*BORDER + 1,
                    state->h * TILE_SIZE + 2*BORDER + 1);
        ds->started = TRUE;
    }

    if (flashtime > 0 && 
        (flashtime <= FLASH_TIME/3 ||
         flashtime >= FLASH_TIME*2/3)) {
        flash_changed = !ds->flashing;
        ds->flashing = TRUE;
        line_colour = COL_HIGHLIGHT;
    } else {
        flash_changed = ds->flashing;
        ds->flashing = FALSE;
        line_colour = COL_FOREGROUND;
    }

#define CROSS_SIZE (3 * LINEWIDTH / 2)
    
    /* Redraw clue colours if necessary */
    for (j = 0; j < h; ++j) {
        for (i = 0; i < w; ++i) {
            c[0] = CLUE_AT(state, i, j);
            c[1] = '\0';
            if (c[0] == ' ')
                continue;

            n = c[0] - '0';
            assert(n >= 0 && n <= 4);

            clue_mistake = (square_order(state, i, j, LINE_YES) > n     || 
                            square_order(state, i, j, LINE_NO ) > (4-n));

            if (clue_mistake != ds->clue_error[j * w + i]) {
                draw_rect(dr, 
                          BORDER + i * TILE_SIZE + CROSS_SIZE,
                          BORDER + j * TILE_SIZE + CROSS_SIZE,
                          TILE_SIZE - CROSS_SIZE * 2, TILE_SIZE - CROSS_SIZE * 2,
                          COL_BACKGROUND);
                draw_text(dr, 
                          BORDER + i * TILE_SIZE + TILE_SIZE/2,
                          BORDER + j * TILE_SIZE + TILE_SIZE/2,
                          FONT_VARIABLE, TILE_SIZE/2, 
                          ALIGN_VCENTRE | ALIGN_HCENTRE, 
                          clue_mistake ? COL_MISTAKE : COL_FOREGROUND, c);
                draw_update(dr, i * TILE_SIZE + BORDER, j * TILE_SIZE + BORDER,
                            TILE_SIZE, TILE_SIZE);

                ds->clue_error[j * w + i] = clue_mistake;
            }
        }
    }

    /* I've also had a request to colour lines red if they make a non-solution
     * loop, or if more than two lines go into any point.  I think that would
     * be good some time. */

#define CLEAR_VL(i, j) do {                                                \
                           draw_rect(dr,                                   \
                                 BORDER + i * TILE_SIZE - CROSS_SIZE,      \
                                 BORDER + j * TILE_SIZE + LINEWIDTH - LINEWIDTH/2,     \
                                 CROSS_SIZE * 2,                           \
                                 TILE_SIZE - LINEWIDTH,                    \
                                 COL_BACKGROUND);                          \
                           draw_update(dr,                                 \
				       BORDER + i * TILE_SIZE - CROSS_SIZE, \
				       BORDER + j * TILE_SIZE - CROSS_SIZE, \
				       CROSS_SIZE*2,                       \
				       TILE_SIZE + CROSS_SIZE*2);          \
                        } while (0)

#define CLEAR_HL(i, j) do {                                                \
                           draw_rect(dr,                                   \
                                 BORDER + i * TILE_SIZE + LINEWIDTH - LINEWIDTH/2,     \
                                 BORDER + j * TILE_SIZE - CROSS_SIZE,      \
                                 TILE_SIZE - LINEWIDTH,                    \
                                 CROSS_SIZE * 2,                           \
                                 COL_BACKGROUND);                          \
                           draw_update(dr,                                 \
				       BORDER + i * TILE_SIZE - CROSS_SIZE, \
				       BORDER + j * TILE_SIZE - CROSS_SIZE, \
				       TILE_SIZE + CROSS_SIZE*2,           \
				       CROSS_SIZE*2);                      \
                        } while (0)

    /* Vertical lines */
    for (j = 0; j < h; ++j) {
        for (i = 0; i < w + 1; ++i) {
            switch (BELOW_DOT(state, i, j)) {
                case LINE_UNKNOWN:
                    if (ds->vl[i + (w + 1) * j] != BELOW_DOT(state, i, j)) {
                        CLEAR_VL(i, j);
                    }
                    break;
                case LINE_YES:
                    if (ds->vl[i + (w + 1) * j] != BELOW_DOT(state, i, j) ||
                        flash_changed) {
                        CLEAR_VL(i, j);
                        draw_rect(dr,
                                  BORDER + i * TILE_SIZE - LINEWIDTH/2,
                                  BORDER + j * TILE_SIZE + LINEWIDTH - LINEWIDTH/2,
                                  LINEWIDTH, TILE_SIZE - LINEWIDTH, 
                                  line_colour);
                    }
                    break;
                case LINE_NO:
                    if (ds->vl[i + (w + 1) * j] != BELOW_DOT(state, i, j)) {
                        CLEAR_VL(i, j);
                        draw_line(dr,
                                 BORDER + i * TILE_SIZE - CROSS_SIZE,
                                 BORDER + j * TILE_SIZE + TILE_SIZE/2 - CROSS_SIZE,
                                 BORDER + i * TILE_SIZE + CROSS_SIZE - 1,
                                 BORDER + j * TILE_SIZE + TILE_SIZE/2 + CROSS_SIZE - 1,
                                  COL_FOREGROUND);
                        draw_line(dr,
                                 BORDER + i * TILE_SIZE + CROSS_SIZE - 1,
                                 BORDER + j * TILE_SIZE + TILE_SIZE/2 - CROSS_SIZE,
                                 BORDER + i * TILE_SIZE - CROSS_SIZE,
                                 BORDER + j * TILE_SIZE + TILE_SIZE/2 + CROSS_SIZE - 1,
                                  COL_FOREGROUND);
                    }
                    break;
            }
            ds->vl[i + (w + 1) * j] = BELOW_DOT(state, i, j);
        }
    }

    /* Horizontal lines */
    for (j = 0; j < h + 1; ++j) {
        for (i = 0; i < w; ++i) {
            switch (RIGHTOF_DOT(state, i, j)) {
                case LINE_UNKNOWN:
                    if (ds->hl[i + w * j] != RIGHTOF_DOT(state, i, j)) {
                        CLEAR_HL(i, j);
                }
                        break;
                case LINE_YES:
                    if (ds->hl[i + w * j] != RIGHTOF_DOT(state, i, j) ||
                        flash_changed) {
                        CLEAR_HL(i, j);
                        draw_rect(dr,
                                  BORDER + i * TILE_SIZE + LINEWIDTH - LINEWIDTH/2,
                                  BORDER + j * TILE_SIZE - LINEWIDTH/2,
                                  TILE_SIZE - LINEWIDTH, LINEWIDTH, 
                                  line_colour);
                        break;
                    }
                case LINE_NO:
                    if (ds->hl[i + w * j] != RIGHTOF_DOT(state, i, j)) {
                        CLEAR_HL(i, j);
                        draw_line(dr,
                                 BORDER + i * TILE_SIZE + TILE_SIZE/2 - CROSS_SIZE,
                                 BORDER + j * TILE_SIZE + CROSS_SIZE - 1,
                                 BORDER + i * TILE_SIZE + TILE_SIZE/2 + CROSS_SIZE - 1,
                                 BORDER + j * TILE_SIZE - CROSS_SIZE,
                                  COL_FOREGROUND);
                        draw_line(dr,
                                 BORDER + i * TILE_SIZE + TILE_SIZE/2 - CROSS_SIZE,
                                 BORDER + j * TILE_SIZE - CROSS_SIZE,
                                 BORDER + i * TILE_SIZE + TILE_SIZE/2 + CROSS_SIZE - 1,
                                 BORDER + j * TILE_SIZE + CROSS_SIZE - 1,
                                  COL_FOREGROUND);
                        break;
                    }
            }
            ds->hl[i + w * j] = RIGHTOF_DOT(state, i, j);
        }
    }
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
                              int dir, game_ui *ui)
{
    return 0.0F;
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

static int game_wants_statusbar(void)
{
    return FALSE;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 7mm squares by default.
     */
    game_compute_size(params, 700, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int w = state->w, h = state->h;
    int ink = print_mono_colour(dr, 0);
    int x, y;
    game_drawstate ads, *ds = &ads;

    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Dots. I'll deliberately make the dots a bit wider than the
     * lines, so you can still see them. (And also because it's
     * annoyingly tricky to make them _exactly_ the same size...)
     */
    for (y = 0; y <= h; y++)
	for (x = 0; x <= w; x++)
	    draw_circle(dr, BORDER + x * TILE_SIZE, BORDER + y * TILE_SIZE,
			LINEWIDTH, ink, ink);

    /*
     * Clues.
     */
    for (y = 0; y < h; y++)
	for (x = 0; x < w; x++)
	    if (CLUE_AT(state, x, y) != ' ') {
		char c[2];

                c[0] = CLUE_AT(state, x, y);
                c[1] = '\0';
                draw_text(dr, 
                          BORDER + x * TILE_SIZE + TILE_SIZE/2,
                          BORDER + y * TILE_SIZE + TILE_SIZE/2,
                          FONT_VARIABLE, TILE_SIZE/2, 
                          ALIGN_VCENTRE | ALIGN_HCENTRE, ink, c);
	    }

    /*
     * Lines. (At the moment, I'm not bothering with crosses.)
     */
    for (y = 0; y <= h; y++)
	for (x = 0; x < w; x++)
	    if (RIGHTOF_DOT(state, x, y) == LINE_YES)
		draw_rect(dr, BORDER + x * TILE_SIZE,
			  BORDER + y * TILE_SIZE - LINEWIDTH/2,
			  TILE_SIZE, (LINEWIDTH/2) * 2 + 1, ink);
    for (y = 0; y < h; y++)
	for (x = 0; x <= w; x++)
	    if (BELOW_DOT(state, x, y) == LINE_YES)
		draw_rect(dr, BORDER + x * TILE_SIZE - LINEWIDTH/2,
			  BORDER + y * TILE_SIZE,
			  (LINEWIDTH/2) * 2 + 1, TILE_SIZE, ink);
}

#ifdef COMBINED
#define thegame loopy
#endif

const struct game thegame = {
    "Loopy", "games.loopy",
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
    TRUE, game_text_format,
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
    game_wants_statusbar,
    FALSE, game_timing_state,
    0,                                       /* mouse_priorities */
};
