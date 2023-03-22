/*
 * magnets.c: implementation of janko.at 'magnets puzzle' game.
 *
 * http://64.233.179.104/translate_c?hl=en&u=http://www.janko.at/Raetsel/Magnete/Beispiel.htm
 *
 * Puzzle definition is just the size, and then the list of + (across then
 * down) and - (across then down) present, then domino edges.
 *
 * An example:
 *
 *  + 2 0 1
 *   +-----+
 *  1|+ -| |1
 *   |-+-+ |
 *  0|-|#| |1
 *   | +-+-|
 *  2|+|- +|1
 *   +-----+
 *    1 2 0 -
 *
 * 3x3:201,102,120,111,LRTT*BBLR
 *
 * 'Zotmeister' examples:
 * 5x5:.2..1,3..1.,.2..2,2..2.,LRLRTTLRTBBT*BTTBLRBBLRLR
 * 9x9:3.51...33,.2..23.13,..33.33.2,12...5.3.,**TLRTLR*,*TBLRBTLR,TBLRLRBTT,BLRTLRTBB,LRTB*TBLR,LRBLRBLRT,TTTLRLRTB,BBBTLRTB*,*LRBLRB**
 *
 * Janko 6x6 with solution:
 * 6x6:322223,323132,232223,232223,LRTLRTTTBLRBBBTTLRLRBBLRTTLRTTBBLRBB
 *
 * janko 8x8:
 * 8x8:34131323,23131334,43122323,21332243,LRTLRLRT,LRBTTTTB,LRTBBBBT,TTBTLRTB,BBTBTTBT,TTBTBBTB,BBTBLRBT,LRBLRLRB
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

#include "puzzles.h"

#ifdef STANDALONE_SOLVER
static bool verbose = false;
#endif

enum {
    COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT,
    COL_TEXT, COL_ERROR, COL_CURSOR, COL_DONE,
    COL_NEUTRAL, COL_NEGATIVE, COL_POSITIVE, COL_NOT,
    NCOLOURS
};

/* Cell states. */
enum { EMPTY = 0, NEUTRAL = EMPTY, POSITIVE = 1, NEGATIVE = 2 };

#if defined DEBUGGING || defined STANDALONE_SOLVER
static const char *cellnames[3] = { "neutral", "positive", "negative" };
#define NAME(w) ( ((w) < 0 || (w) > 2) ? "(out of range)" : cellnames[(w)] )
#endif

#define GRID2CHAR(g) ( ((g) >= 0 && (g) <= 2) ? ".+-"[(g)] : '?' )
#define CHAR2GRID(c) ( (c) == '+' ? POSITIVE : (c) == '-' ? NEGATIVE : NEUTRAL )

#define INGRID(s,x,y) ((x) >= 0 && (x) < (s)->w && (y) >= 0 && (y) < (s)->h)

#define OPPOSITE(x) ( ((x)*2) % 3 ) /* 0 --> 0,
                                       1 --> 2,
                                       2 --> 4 --> 1 */

#define FLASH_TIME 0.7F

/* Macro ickery copied from slant.c */
#define DIFFLIST(A) \
    A(EASY,Easy,e) \
    A(TRICKY,Tricky,t)
#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const magnets_diffnames[] = { DIFFLIST(TITLE) "(count)" };
static char const magnets_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)


/* --------------------------------------------------------------- */
/* Game parameter functions. */

struct game_params {
    int w, h, diff;
    bool stripclues;
};

#define DEFAULT_PRESET 2

static const struct game_params magnets_presets[] = {
    {6, 5, DIFF_EASY, 0},
    {6, 5, DIFF_TRICKY, 0},
    {6, 5, DIFF_TRICKY, 1},
    {8, 7, DIFF_EASY, 0},
    {8, 7, DIFF_TRICKY, 0},
    {8, 7, DIFF_TRICKY, 1},
    {10, 9, DIFF_TRICKY, 0},
    {10, 9, DIFF_TRICKY, 1}
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    *ret = magnets_presets[DEFAULT_PRESET];

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[64];

    if (i < 0 || i >= lenof(magnets_presets)) return false;

    ret = default_params();
    *ret = magnets_presets[i]; /* struct copy */
    *params = ret;

    sprintf(buf, "%dx%d %s%s",
            magnets_presets[i].w, magnets_presets[i].h,
            magnets_diffnames[magnets_presets[i].diff],
            magnets_presets[i].stripclues ? ", strip clues" : "");
    *name = dupstr(buf);

    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;       /* structure copy */
    return ret;
}

static void decode_params(game_params *ret, char const *string)
{
    ret->w = ret->h = atoi(string);
    while (*string && isdigit((unsigned char) *string)) ++string;
    if (*string == 'x') {
        string++;
        ret->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }

    ret->diff = DIFF_EASY;
    if (*string == 'd') {
	int i;
	string++;
	for (i = 0; i < DIFFCOUNT; i++)
	    if (*string == magnets_diffchars[i])
		ret->diff = i;
	if (*string) string++;
    }

    ret->stripclues = false;
    if (*string == 'S') {
        string++;
        ret->stripclues = true;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[256];
    sprintf(buf, "%dx%d", params->w, params->h);
    if (full)
        sprintf(buf + strlen(buf), "d%c%s",
                magnets_diffchars[params->diff],
                params->stripclues ? "S" : "");
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[64];

    ret = snewn(5, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].u.string.sval = dupstr(buf);

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[1].u.string.sval = dupstr(buf);

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].u.choices.choicenames = DIFFCONFIG;
    ret[2].u.choices.selected = params->diff;

    ret[3].name = "Strip clues";
    ret[3].type = C_BOOLEAN;
    ret[3].u.boolean.bval = params->stripclues;

    ret[4].name = NULL;
    ret[4].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->diff = cfg[2].u.choices.selected;
    ret->stripclues = cfg[3].u.boolean.bval;

    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 2) return "Width must be at least two";
    if (params->h < 2) return "Height must be at least two";
    if (params->w > INT_MAX / params->h)
        return "Width times height must not be unreasonably large";
    if (params->diff >= DIFF_TRICKY) {
        if (params->w < 5 && params->h < 5)
            return "Either width or height must be at least five for Tricky";
    } else {
        if (params->w < 3 && params->h < 3)
            return "Either width or height must be at least three";
    }
    if (params->diff < 0 || params->diff >= DIFFCOUNT)
        return "Unknown difficulty level";

    return NULL;
}

/* --------------------------------------------------------------- */
/* Game state allocation, deallocation. */

struct game_common {
    int *dominoes;      /* size w*h, dominoes[i] points to other end of domino. */
    int *rowcount;      /* size 3*h, array of [plus, minus, neutral] counts */
    int *colcount;      /* size 3*w, ditto */
    int refcount;
};

#define GS_ERROR        1
#define GS_SET          2
#define GS_NOTPOSITIVE  4
#define GS_NOTNEGATIVE  8
#define GS_NOTNEUTRAL  16
#define GS_MARK        32

#define GS_NOTMASK (GS_NOTPOSITIVE|GS_NOTNEGATIVE|GS_NOTNEUTRAL)

#define NOTFLAG(w) ( (w) == NEUTRAL ? GS_NOTNEUTRAL : \
                     (w) == POSITIVE ? GS_NOTPOSITIVE : \
                     (w) == NEGATIVE ? GS_NOTNEGATIVE : \
                     0 )

#define POSSIBLE(f,w) (!(state->flags[(f)] & NOTFLAG(w)))

struct game_state {
    int w, h, wh;
    int *grid;                  /* size w*h, for cell state (pos/neg) */
    unsigned int *flags;        /* size w*h */
    bool solved, completed, numbered;
    bool *counts_done;

    struct game_common *common; /* domino layout never changes. */
};

static void clear_state(game_state *ret)
{
    int i;

    ret->solved = false;
    ret->completed = false;
    ret->numbered = false;

    memset(ret->common->rowcount, 0, ret->h*3*sizeof(int));
    memset(ret->common->colcount, 0, ret->w*3*sizeof(int));
    memset(ret->counts_done, 0, (ret->h + ret->w) * 2 * sizeof(bool));

    for (i = 0; i < ret->wh; i++) {
        ret->grid[i] = EMPTY;
        ret->flags[i] = 0;
        ret->common->dominoes[i] = i;
    }
}

static game_state *new_state(int w, int h)
{
    game_state *ret = snew(game_state);

    memset(ret, 0, sizeof(game_state));
    ret->w = w;
    ret->h = h;
    ret->wh = w*h;

    ret->grid = snewn(ret->wh, int);
    ret->flags = snewn(ret->wh, unsigned int);
    ret->counts_done = snewn((ret->h + ret->w) * 2, bool);

    ret->common = snew(struct game_common);
    ret->common->refcount = 1;

    ret->common->dominoes = snewn(ret->wh, int);
    ret->common->rowcount = snewn(ret->h*3, int);
    ret->common->colcount = snewn(ret->w*3, int);

    clear_state(ret);

    return ret;
}

static game_state *dup_game(const game_state *src)
{
    game_state *dest = snew(game_state);

    dest->w = src->w;
    dest->h = src->h;
    dest->wh = src->wh;

    dest->solved = src->solved;
    dest->completed = src->completed;
    dest->numbered = src->numbered;

    dest->common = src->common;
    dest->common->refcount++;

    dest->grid = snewn(dest->wh, int);
    memcpy(dest->grid, src->grid, dest->wh*sizeof(int));

    dest->counts_done = snewn((dest->h + dest->w) * 2, bool);
    memcpy(dest->counts_done, src->counts_done,
           (dest->h + dest->w) * 2 * sizeof(bool));

    dest->flags = snewn(dest->wh, unsigned int);
    memcpy(dest->flags, src->flags, dest->wh*sizeof(unsigned int));

    return dest;
}

static void free_game(game_state *state)
{
    state->common->refcount--;
    if (state->common->refcount == 0) {
        sfree(state->common->dominoes);
        sfree(state->common->rowcount);
        sfree(state->common->colcount);
        sfree(state->common);
    }
    sfree(state->counts_done);
    sfree(state->flags);
    sfree(state->grid);
    sfree(state);
}

/* --------------------------------------------------------------- */
/* Game generation and reading. */

/* For a game of size w*h the game description is:
 * w-sized string of column + numbers (L-R), or '.' for none
 * semicolon
 * h-sized string of row + numbers (T-B), or '.'
 * semicolon
 * w-sized string of column - numbers (L-R), or '.'
 * semicolon
 * h-sized string of row - numbers (T-B), or '.'
 * semicolon
 * w*h-sized string of 'L', 'R', 'U', 'D' for domino associations,
 *   or '*' for a black singleton square.
 *
 * for a total length of 2w + 2h + wh + 4.
 */

static char n2c(int num) { /* XXX cloned from singles.c */
    if (num == -1)
        return '.';
    if (num < 10)
        return '0' + num;
    else if (num < 10+26)
        return 'a' + num - 10;
    else
        return 'A' + num - 10 - 26;
    return '?';
}

static int c2n(char c) { /* XXX cloned from singles.c */
    if (isdigit((unsigned char)c))
        return (int)(c - '0');
    else if (c >= 'a' && c <= 'z')
        return (int)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'Z')
        return (int)(c - 'A' + 10 + 26);
    return -1;
}

static const char *readrow(const char *desc, int n, int *array, int off,
                           const char **prob)
{
    int i, num;
    char c;

    for (i = 0; i < n; i++) {
        c = *desc++;
        if (c == 0) goto badchar;
        if (c == '.')
            num = -1;
        else {
            num = c2n(c);
            if (num < 0) goto badchar;
        }
        array[i*3+off] = num;
    }
    c = *desc++;
    if (c != ',') goto badchar;
    return desc;

badchar:
    *prob = (c == 0) ?
                "Game description too short" :
                "Game description contained unexpected characters";
    return NULL;
}

static game_state *new_game_int(const game_params *params, const char *desc,
                                const char **prob)
{
    game_state *state = new_state(params->w, params->h);
    int x, y, idx, *count;
    char c;

    *prob = NULL;

    /* top row, left-to-right */
    desc = readrow(desc, state->w, state->common->colcount, POSITIVE, prob);
    if (*prob) goto done;

    /* left column, top-to-bottom */
    desc = readrow(desc, state->h, state->common->rowcount, POSITIVE, prob);
    if (*prob) goto done;

    /* bottom row, left-to-right */
    desc = readrow(desc, state->w, state->common->colcount, NEGATIVE, prob);
    if (*prob) goto done;

    /* right column, top-to-bottom */
    desc = readrow(desc, state->h, state->common->rowcount, NEGATIVE, prob);
    if (*prob) goto done;

    /* Add neutral counts (== size - pos - neg) to columns and rows.
     * Any singleton cells will just be treated as permanently neutral. */
    count = state->common->colcount;
    for (x = 0; x < state->w; x++) {
        if (count[x*3+POSITIVE] < 0 || count[x*3+NEGATIVE] < 0)
            count[x*3+NEUTRAL] = -1;
        else {
            count[x*3+NEUTRAL] =
                state->h - count[x*3+POSITIVE] - count[x*3+NEGATIVE];
            if (count[x*3+NEUTRAL] < 0) {
                *prob = "Column counts inconsistent";
                goto done;
            }
        }
    }
    count = state->common->rowcount;
    for (y = 0; y < state->h; y++) {
        if (count[y*3+POSITIVE] < 0 || count[y*3+NEGATIVE] < 0)
            count[y*3+NEUTRAL] = -1;
        else {
            count[y*3+NEUTRAL] =
                state->w - count[y*3+POSITIVE] - count[y*3+NEGATIVE];
            if (count[y*3+NEUTRAL] < 0) {
                *prob = "Row counts inconsistent";
                goto done;
            }
        }
    }


    for (y = 0; y < state->h; y++) {
        for (x = 0; x < state->w; x++) {
            idx = y*state->w + x;
nextchar:
            c = *desc++;

            if (c == 'L') /* this square is LHS of a domino */
                state->common->dominoes[idx] = idx+1;
            else if (c == 'R') /* ... RHS of a domino */
                state->common->dominoes[idx] = idx-1;
            else if (c == 'T') /* ... top of a domino */
                state->common->dominoes[idx] = idx+state->w;
            else if (c == 'B') /* ... bottom of a domino */
                state->common->dominoes[idx] = idx-state->w;
            else if (c == '*') /* singleton */
                state->common->dominoes[idx] = idx;
            else if (c == ',') /* spacer, ignore */
                goto nextchar;
            else goto badchar;
        }
    }

    /* Check dominoes as input are sensibly consistent
     * (i.e. each end points to the other) */
    for (idx = 0; idx < state->wh; idx++) {
        if (state->common->dominoes[idx] < 0 ||
            state->common->dominoes[idx] > state->wh ||
            state->common->dominoes[state->common->dominoes[idx]] != idx) {
            *prob = "Domino descriptions inconsistent";
            goto done;
        }
        if (state->common->dominoes[idx] == idx) {
            state->grid[idx] = NEUTRAL;
            state->flags[idx] |= GS_SET;
        }
    }
    /* Success. */
    state->numbered = true;
    goto done;

badchar:
    *prob = (c == 0) ?
                "Game description too short" :
                "Game description contained unexpected characters";

done:
    if (*prob) {
        free_game(state);
        return NULL;
    }
    return state;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    const char *prob;
    game_state *st = new_game_int(params, desc, &prob);
    if (!st) return prob;
    free_game(st);
    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    const char *prob;
    game_state *st = new_game_int(params, desc, &prob);
    assert(st);
    return st;
}

static char *generate_desc(game_state *new)
{
    int x, y, idx, other, w = new->w, h = new->h;
    char *desc = snewn(new->wh + 2*(w + h) + 5, char), *p = desc;

    for (x = 0; x < w; x++) *p++ = n2c(new->common->colcount[x*3+POSITIVE]);
    *p++ = ',';
    for (y = 0; y < h; y++) *p++ = n2c(new->common->rowcount[y*3+POSITIVE]);
    *p++ = ',';

    for (x = 0; x < w; x++) *p++ = n2c(new->common->colcount[x*3+NEGATIVE]);
    *p++ = ',';
    for (y = 0; y < h; y++) *p++ = n2c(new->common->rowcount[y*3+NEGATIVE]);
    *p++ = ',';

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            idx = y*w + x;
            other = new->common->dominoes[idx];

            if (other == idx) *p++ = '*';
            else if (other == idx+1) *p++ = 'L';
            else if (other == idx-1) *p++ = 'R';
            else if (other == idx+w) *p++ = 'T';
            else if (other == idx-w) *p++ = 'B';
            else assert(!"mad domino orientation");
        }
    }
    *p = '\0';

    return desc;
}

static void game_text_hborder(const game_state *state, char **p_r)
{
    char *p = *p_r;
    int x;

    *p++ = ' ';
    *p++ = '+';
    for (x = 0; x < state->w*2-1; x++) *p++ = '-';
    *p++ = '+';
    *p++ = '\n';

    *p_r = p;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int len, x, y, i;
    char *ret, *p;

    len = ((state->w*2)+4) * ((state->h*2)+4) + 2;
    p = ret = snewn(len, char);

    /* top row: '+' then column totals for plus. */
    *p++ = '+';
    for (x = 0; x < state->w; x++) {
        *p++ = ' ';
        *p++ = n2c(state->common->colcount[x*3+POSITIVE]);
    }
    *p++ = '\n';

    /* top border. */
    game_text_hborder(state, &p);

    for (y = 0; y < state->h; y++) {
        *p++ = n2c(state->common->rowcount[y*3+POSITIVE]);
        *p++ = '|';
        for (x = 0; x < state->w; x++) {
            i = y*state->w+x;
            *p++ = state->common->dominoes[i] == i ? '#' :
                state->grid[i] == POSITIVE ? '+' :
                state->grid[i] == NEGATIVE ? '-' :
                state->flags[i] & GS_SET ? '*' : ' ';
            if (x < (state->w-1))
                *p++ = state->common->dominoes[i] == i+1 ? ' ' : '|';
        }
        *p++ = '|';
        *p++ = n2c(state->common->rowcount[y*3+NEGATIVE]);
        *p++ = '\n';

        if (y < (state->h-1)) {
            *p++ = ' ';
            *p++ = '|';
            for (x = 0; x < state->w; x++) {
                i = y*state->w+x;
                *p++ = state->common->dominoes[i] == i+state->w ? ' ' : '-';
                if (x < (state->w-1))
                    *p++ = '+';
            }
            *p++ = '|';
            *p++ = '\n';
        }
    }

    /* bottom border. */
    game_text_hborder(state, &p);

    /* bottom row: column totals for minus then '-'. */
    *p++ = ' ';
    for (x = 0; x < state->w; x++) {
        *p++ = ' ';
        *p++ = n2c(state->common->colcount[x*3+NEGATIVE]);
    }
    *p++ = ' ';
    *p++ = '-';
    *p++ = '\n';
    *p++ = '\0';

    return ret;
}

static void game_debug(game_state *state, const char *desc)
{
    char *fmt = game_text_format(state);
    debug(("%s:\n%s\n", desc, fmt));
    sfree(fmt);
}

enum { ROW, COLUMN };

typedef struct rowcol {
    int i, di, n, roworcol, num;
    int *targets;
    const char *name;
} rowcol;

static rowcol mkrowcol(const game_state *state, int num, int roworcol)
{
    rowcol rc;

    rc.roworcol = roworcol;
    rc.num = num;

    if (roworcol == ROW) {
        rc.i = num * state->w;
        rc.di = 1;
        rc.n = state->w;
        rc.targets = &(state->common->rowcount[num*3]);
        rc.name = "row";
    } else if (roworcol == COLUMN) {
        rc.i = num;
        rc.di = state->w;
        rc.n = state->h;
        rc.targets = &(state->common->colcount[num*3]);
        rc.name = "column";
    } else {
        assert(!"unknown roworcol");
    }
    return rc;
}

static int count_rowcol(const game_state *state, int num, int roworcol,
                        int which)
{
    int i, count = 0;
    rowcol rc = mkrowcol(state, num, roworcol);

    for (i = 0; i < rc.n; i++, rc.i += rc.di) {
        if (which < 0) {
            if (state->grid[rc.i] == EMPTY &&
                !(state->flags[rc.i] & GS_SET))
                count++;
        } else if (state->grid[rc.i] == which)
            count++;
    }
    return count;
}

static void check_rowcol(game_state *state, int num, int roworcol, int which,
                         bool *wrong, bool *incomplete)
{
    int count, target = mkrowcol(state, num, roworcol).targets[which];

    if (target == -1) return; /* no number to check against. */

    count = count_rowcol(state, num, roworcol, which);
    if (count < target) *incomplete = true;
    if (count > target) *wrong = true;
}

static int check_completion(game_state *state)
{
    int i, j, x, y, idx, w = state->w, h = state->h;
    int which = POSITIVE;
    bool wrong = false, incomplete = false;

    /* Check row and column counts for magnets. */
    for (which = POSITIVE, j = 0; j < 2; which = OPPOSITE(which), j++) {
        for (i = 0; i < w; i++)
            check_rowcol(state, i, COLUMN, which, &wrong, &incomplete);

        for (i = 0; i < h; i++)
            check_rowcol(state, i, ROW, which, &wrong, &incomplete);
    }
    /* Check each domino has been filled, and that we don't have
     * touching identical terminals. */
    for (i = 0; i < state->wh; i++) state->flags[i] &= ~GS_ERROR;
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            idx = y*w + x;
            if (state->common->dominoes[idx] == idx)
                continue; /* no domino here */

            if (!(state->flags[idx] & GS_SET))
                incomplete = true;

            which = state->grid[idx];
            if (which != NEUTRAL) {
#define CHECK(xx,yy) do { \
    if (INGRID(state,xx,yy) && \
        (state->grid[(yy)*w+(xx)] == which)) { \
        wrong = true; \
        state->flags[(yy)*w+(xx)] |= GS_ERROR; \
        state->flags[y*w+x] |= GS_ERROR; \
    } \
} while(0)
                CHECK(x,y-1);
                CHECK(x,y+1);
                CHECK(x-1,y);
                CHECK(x+1,y);
#undef CHECK
            }
        }
    }
    return wrong ? -1 : incomplete ? 0 : 1;
}

static const int dx[4] = {-1, 1, 0, 0};
static const int dy[4] = {0, 0, -1, 1};

static void solve_clearflags(game_state *state)
{
    int i;

    for (i = 0; i < state->wh; i++) {
        state->flags[i] &= ~GS_NOTMASK;
        if (state->common->dominoes[i] != i)
            state->flags[i] &= ~GS_SET;
    }
}

/* Knowing a given cell cannot be a certain colour also tells us
 * something about the other cell in that domino. */
static int solve_unflag(game_state *state, int i, int which,
                        const char *why, rowcol *rc)
{
    int ii, ret = 0;
#if defined DEBUGGING || defined STANDALONE_SOLVER
    int w = state->w;
#endif

    assert(i >= 0 && i < state->wh);
    ii = state->common->dominoes[i];
    if (ii == i) return 0;

    if (rc)
        debug(("solve_unflag: (%d,%d) for %s %d", i%w, i/w, rc->name, rc->num));

    if ((state->flags[i] & GS_SET) && (state->grid[i] == which)) {
        debug(("solve_unflag: (%d,%d) already %s, cannot unflag (for %s).",
               i%w, i/w, NAME(which), why));
        return -1;
    }
    if ((state->flags[ii] & GS_SET) && (state->grid[ii] == OPPOSITE(which))) {
        debug(("solve_unflag: (%d,%d) opposite already %s, cannot unflag (for %s).",
               ii%w, ii/w, NAME(OPPOSITE(which)), why));
        return -1;
    }
    if (POSSIBLE(i, which)) {
        state->flags[i] |= NOTFLAG(which);
        ret++;
        debug(("solve_unflag: (%d,%d) CANNOT be %s (%s)",
               i%w, i/w, NAME(which), why));
    }
    if (POSSIBLE(ii, OPPOSITE(which))) {
        state->flags[ii] |= NOTFLAG(OPPOSITE(which));
        ret++;
        debug(("solve_unflag: (%d,%d) CANNOT be %s (%s, other half)",
               ii%w, ii/w, NAME(OPPOSITE(which)), why));
    }
#ifdef STANDALONE_SOLVER
    if (verbose && ret) {
        printf("(%d,%d)", i%w, i/w);
        if (rc) printf(" in %s %d", rc->name, rc->num);
        printf(" cannot be %s (%s); opposite (%d,%d) not %s.\n",
               NAME(which), why, ii%w, ii/w, NAME(OPPOSITE(which)));
    }
#endif
    return ret;
}

static int solve_unflag_surrounds(game_state *state, int i, int which)
{
    int x = i%state->w, y = i/state->w, xx, yy, j, ii;

    assert(INGRID(state, x, y));

    for (j = 0; j < 4; j++) {
        xx = x+dx[j]; yy = y+dy[j];
        if (!INGRID(state, xx, yy)) continue;

        ii = yy*state->w+xx;
        if (solve_unflag(state, ii, which, "adjacent to set cell", NULL) < 0)
            return -1;
    }
    return 0;
}

/* Sets a cell to a particular colour, and also perform other
 * housekeeping around that. */
static int solve_set(game_state *state, int i, int which,
                     const char *why, rowcol *rc)
{
    int ii;
#if defined DEBUGGING || defined STANDALONE_SOLVER
    int w = state->w;
#endif

    ii = state->common->dominoes[i];

    if (state->flags[i] & GS_SET) {
        if (state->grid[i] == which) {
            return 0; /* was already set and held, do nothing. */
        } else {
            debug(("solve_set: (%d,%d) is held and %s, cannot set to %s",
                   i%w, i/w, NAME(state->grid[i]), NAME(which)));
            return -1;
        }
    }
    if ((state->flags[ii] & GS_SET) && state->grid[ii] != OPPOSITE(which)) {
        debug(("solve_set: (%d,%d) opposite is held and %s, cannot set to %s",
                ii%w, ii/w, NAME(state->grid[ii]), NAME(OPPOSITE(which))));
        return -1;
    }
    if (!POSSIBLE(i, which)) {
        debug(("solve_set: (%d,%d) NOT %s, cannot set.", i%w, i/w, NAME(which)));
        return -1;
    }
    if (!POSSIBLE(ii, OPPOSITE(which))) {
        debug(("solve_set: (%d,%d) NOT %s, cannot set (%d,%d).",
               ii%w, ii/w, NAME(OPPOSITE(which)), i%w, i/w));
        return -1;
    }

#ifdef STANDALONE_SOLVER
    if (verbose) {
        printf("(%d,%d)", i%w, i/w);
        if (rc) printf(" in %s %d", rc->name, rc->num);
        printf(" set to %s (%s), opposite (%d,%d) set to %s.\n",
               NAME(which), why, ii%w, ii/w, NAME(OPPOSITE(which)));
    }
#endif
    if (rc)
        debug(("solve_set: (%d,%d) for %s %d", i%w, i/w, rc->name, rc->num));
    debug(("solve_set: (%d,%d) setting to %s (%s), surrounds first:",
           i%w, i/w, NAME(which), why));

    if (which != NEUTRAL) {
        if (solve_unflag_surrounds(state, i, which) < 0)
            return -1;
        if (solve_unflag_surrounds(state, ii, OPPOSITE(which)) < 0)
            return -1;
    }

    state->grid[i] = which;
    state->grid[ii] = OPPOSITE(which);

    state->flags[i] |= GS_SET;
    state->flags[ii] |= GS_SET;

    debug(("solve_set: (%d,%d) set to %s (%s)", i%w, i/w, NAME(which), why));

    return 1;
}

/* counts should be int[4]. */
static void solve_counts(game_state *state, rowcol rc, int *counts, int *unset)
{
    int i, j, which;

    assert(counts);
    for (i = 0; i < 4; i++) {
        counts[i] = 0;
        if (unset) unset[i] = 0;
    }

    for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
        if (state->flags[i] & GS_SET) {
            assert(state->grid[i] < 3);
            counts[state->grid[i]]++;
        } else if (unset) {
            for (which = 0; which <= 2; which++) {
                if (POSSIBLE(i, which))
                    unset[which]++;
            }
        }
    }
}

static int solve_checkfull(game_state *state, rowcol rc, int *counts)
{
    int starti = rc.i, j, which, didsth = 0, target;
    int unset[4];

    assert(state->numbered); /* only useful (should only be called) if numbered. */

    solve_counts(state, rc, counts, unset);

    for (which = 0; which <= 2; which++) {
        target = rc.targets[which];
        if (target == -1) continue;

        /*debug(("%s %d for %s: target %d, count %d, unset %d",
               rc.name, rc.num, NAME(which),
               target, counts[which], unset[which]));*/

        if (target < counts[which]) {
            debug(("%s %d has too many (%d) %s squares (target %d), impossible!",
                   rc.name, rc.num, counts[which], NAME(which), target));
            return -1;
        }
        if (target == counts[which]) {
            /* We have the correct no. of the colour in this row/column
             * already; unflag all the rest. */
            for (rc.i = starti, j = 0; j < rc.n; rc.i += rc.di, j++) {
                if (state->flags[rc.i] & GS_SET) continue;
                if (!POSSIBLE(rc.i, which)) continue;

                if (solve_unflag(state, rc.i, which, "row/col full", &rc) < 0)
                    return -1;
                didsth = 1;
            }
        } else if ((target - counts[which]) == unset[which]) {
            /* We need all the remaining unset squares for this colour;
             * set them all. */
            for (rc.i = starti, j = 0; j < rc.n; rc.i += rc.di, j++) {
                if (state->flags[rc.i] & GS_SET) continue;
                if (!POSSIBLE(rc.i, which)) continue;

                if (solve_set(state, rc.i, which, "row/col needs all unset", &rc) < 0)
                    return -1;
                didsth = 1;
            }
        }
    }
    return didsth;
}

static int solve_startflags(game_state *state)
{
    int x, y, i;

    for (x = 0; x < state->w; x++) {
        for (y = 0; y < state->h; y++) {
            i = y*state->w+x;
            if (state->common->dominoes[i] == i) continue;
            if (state->grid[i] != NEUTRAL ||
                state->flags[i] & GS_SET) {
                if (solve_set(state, i, state->grid[i], "initial set-and-hold", NULL) < 0)
                    return -1;
            }
        }
    }
    return 0;
}

typedef int (*rowcolfn)(game_state *state, rowcol rc, int *counts);

static int solve_rowcols(game_state *state, rowcolfn fn)
{
    int x, y, didsth = 0, ret;
    rowcol rc;
    int counts[4];

    for (x = 0; x < state->w; x++) {
        rc = mkrowcol(state, x, COLUMN);
        solve_counts(state, rc, counts, NULL);

        ret = fn(state, rc, counts);
        if (ret < 0) return ret;
        didsth += ret;
    }
    for (y = 0; y < state->h; y++) {
        rc = mkrowcol(state, y, ROW);
        solve_counts(state, rc, counts, NULL);

        ret = fn(state, rc, counts);
        if (ret < 0) return ret;
        didsth += ret;
    }
    return didsth;
}

static int solve_force(game_state *state)
{
    int i, which, didsth = 0;
    unsigned long f;

    for (i = 0; i < state->wh; i++) {
        if (state->flags[i] & GS_SET) continue;
        if (state->common->dominoes[i] == i) continue;

        f = state->flags[i] & GS_NOTMASK;
        which = -1;
        if (f == (GS_NOTPOSITIVE|GS_NOTNEGATIVE))
            which = NEUTRAL;
        if (f == (GS_NOTPOSITIVE|GS_NOTNEUTRAL))
            which = NEGATIVE;
        if (f == (GS_NOTNEGATIVE|GS_NOTNEUTRAL))
            which = POSITIVE;
        if (which != -1) {
            if (solve_set(state, i, which, "forced by flags", NULL) < 0)
                return -1;
            didsth = 1;
        }
    }
    return didsth;
}

static int solve_neither(game_state *state)
{
    int i, j, didsth = 0;

    for (i = 0; i < state->wh; i++) {
        if (state->flags[i] & GS_SET) continue;
        j = state->common->dominoes[i];
        if (i == j) continue;

        if (((state->flags[i] & GS_NOTPOSITIVE) &&
             (state->flags[j] & GS_NOTPOSITIVE)) ||
            ((state->flags[i] & GS_NOTNEGATIVE) &&
             (state->flags[j] & GS_NOTNEGATIVE))) {
            if (solve_set(state, i, NEUTRAL, "neither tile magnet", NULL) < 0)
                return -1;
            didsth = 1;
        }
    }
    return didsth;
}

static int solve_advancedfull(game_state *state, rowcol rc, int *counts)
{
    int i, j, nfound = 0, ret = 0;
    bool clearpos = false, clearneg = false;

    /* For this row/col, look for a domino entirely within the row where
     * both ends can only be + or - (but isn't held).
     * The +/- counts can thus be decremented by 1 each, and the 'unset'
     * count by 2.
     *
     * Once that's done for all such dominoes (and they're marked), try
     * and made usual deductions about rest of the row based on new totals. */

    if (rc.targets[POSITIVE] == -1 && rc.targets[NEGATIVE] == -1)
        return 0; /* don't have a target for either colour, nothing to do. */
    if ((rc.targets[POSITIVE] >= 0 && counts[POSITIVE] == rc.targets[POSITIVE]) &&
        (rc.targets[NEGATIVE] >= 0 && counts[NEGATIVE] == rc.targets[NEGATIVE]))
        return 0; /* both colours are full up already, nothing to do. */

    for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++)
        state->flags[i] &= ~GS_MARK;

    for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
        if (state->flags[i] & GS_SET) continue;

        /* We're looking for a domino in our row/col, thus if
         * dominoes[i] -> i+di we've found one. */
        if (state->common->dominoes[i] != i+rc.di) continue;

        /* We need both squares of this domino to be either + or -
         * (i.e. both NOTNEUTRAL only). */
        if (((state->flags[i] & GS_NOTMASK) != GS_NOTNEUTRAL) ||
            ((state->flags[i+rc.di] & GS_NOTMASK) != GS_NOTNEUTRAL))
            continue;

        debug(("Domino in %s %d at (%d,%d) must be polarised.",
               rc.name, rc.num, i%state->w, i/state->w));
        state->flags[i] |= GS_MARK;
        state->flags[i+rc.di] |= GS_MARK;
        nfound++;
    }
    if (nfound == 0) return 0;

    /* nfound is #dominoes we matched, which will all be marked. */
    counts[POSITIVE] += nfound;
    counts[NEGATIVE] += nfound;

    if (rc.targets[POSITIVE] >= 0 && counts[POSITIVE] == rc.targets[POSITIVE]) {
        debug(("%s %d has now filled POSITIVE:", rc.name, rc.num));
        clearpos = true;
    }
    if (rc.targets[NEGATIVE] >= 0 && counts[NEGATIVE] == rc.targets[NEGATIVE]) {
        debug(("%s %d has now filled NEGATIVE:", rc.name, rc.num));
        clearneg = true;
    }

    if (!clearpos && !clearneg) return 0;

    for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
        if (state->flags[i] & GS_SET) continue;
        if (state->flags[i] & GS_MARK) continue;

        if (clearpos && !(state->flags[i] & GS_NOTPOSITIVE)) {
            if (solve_unflag(state, i, POSITIVE, "row/col full (+ve) [tricky]", &rc) < 0)
                return -1;
            ret++;
        }
        if (clearneg && !(state->flags[i] & GS_NOTNEGATIVE)) {
            if (solve_unflag(state, i, NEGATIVE, "row/col full (-ve) [tricky]", &rc) < 0)
                return -1;
            ret++;
        }
    }

    return ret;
}

/* If we only have one neutral still to place on a row/column then no
   dominoes entirely in that row/column can be neutral. */
static int solve_nonneutral(game_state *state, rowcol rc, int *counts)
{
    int i, j, ret = 0;

    if (rc.targets[NEUTRAL] != counts[NEUTRAL]+1)
        return 0;

    for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
        if (state->flags[i] & GS_SET) continue;
        if (state->common->dominoes[i] != i+rc.di) continue;

        if (!(state->flags[i] & GS_NOTNEUTRAL)) {
            if (solve_unflag(state, i, NEUTRAL, "single neutral in row/col [tricky]", &rc) < 0)
                return -1;
            ret++;
        }
    }
    return ret;
}

/* If we need to fill all unfilled cells with +-, and we need 1 more of
 * one than the other, and we have a single odd-numbered region of unfilled
 * cells, that odd-numbered region must start and end with the extra number. */
static int solve_oddlength(game_state *state, rowcol rc, int *counts)
{
    int i, j, ret = 0, extra, tpos, tneg;
    int start = -1, length = 0, startodd = -1;
    bool inempty = false;

    /* need zero neutral cells still to find... */
    if (rc.targets[NEUTRAL] != counts[NEUTRAL])
        return 0;

    /* ...and #positive and #negative to differ by one. */
    tpos = rc.targets[POSITIVE] - counts[POSITIVE];
    tneg = rc.targets[NEGATIVE] - counts[NEGATIVE];
    if (tpos == tneg+1)
        extra = POSITIVE;
    else if (tneg == tpos+1)
        extra = NEGATIVE;
    else return 0;

    for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
        if (state->flags[i] & GS_SET) {
            if (inempty) {
                if (length % 2) {
                    /* we've just finished an odd-length section. */
                    if (startodd != -1) goto twoodd;
                    startodd = start;
                }
                inempty = false;
            }
        } else {
            if (inempty)
                length++;
            else {
                start = i;
                length = 1;
                inempty = true;
            }
        }
    }
    if (inempty && (length % 2)) {
        if (startodd != -1) goto twoodd;
        startodd = start;
    }
    if (startodd != -1)
        ret = solve_set(state, startodd, extra, "odd-length section start", &rc);

    return ret;

twoodd:
    debug(("%s %d has >1 odd-length sections, starting at %d,%d and %d,%d.",
           rc.name, rc.num,
           startodd%state->w, startodd/state->w,
           start%state->w, start/state->w));
    return 0;
}

/* Count the number of remaining empty dominoes in any row/col.
 * If that number is equal to the #remaining positive,
 * or to the #remaining negative, no empty cells can be neutral. */
static int solve_countdominoes_neutral(game_state *state, rowcol rc, int *counts)
{
    int i, j, ndom = 0, ret = 0;
    bool nonn = false;

    if ((rc.targets[POSITIVE] == -1) && (rc.targets[NEGATIVE] == -1))
        return 0; /* need at least one target to compare. */

    for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
        if (state->flags[i] & GS_SET) continue;
        assert(state->grid[i] == EMPTY);

        /* Skip solo cells, or second cell in domino. */
        if ((state->common->dominoes[i] == i) ||
            (state->common->dominoes[i] == i-rc.di))
            continue;

        ndom++;
    }

    if ((rc.targets[POSITIVE] != -1) &&
        (rc.targets[POSITIVE]-counts[POSITIVE] == ndom))
        nonn = true;
    if ((rc.targets[NEGATIVE] != -1) &&
        (rc.targets[NEGATIVE]-counts[NEGATIVE] == ndom))
        nonn = true;

    if (!nonn) return 0;

    for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
        if (state->flags[i] & GS_SET) continue;

        if (!(state->flags[i] & GS_NOTNEUTRAL)) {
            if (solve_unflag(state, i, NEUTRAL, "all dominoes +/- [tricky]", &rc) < 0)
                return -1;
            ret++;
        }
    }
    return ret;
}

static int solve_domino_count(game_state *state, rowcol rc, int i, int which)
{
    int nposs = 0;

    /* Skip solo cells or 2nd in domino. */
    if ((state->common->dominoes[i] == i) ||
        (state->common->dominoes[i] == i-rc.di))
        return 0;

    if (state->flags[i] & GS_SET)
        return 0;

    if (POSSIBLE(i, which))
        nposs++;

    if (state->common->dominoes[i] == i+rc.di) {
        /* second cell of domino is on our row: test that too. */
        if (POSSIBLE(i+rc.di, which))
            nposs++;
    }
    return nposs;
}

/* Count number of dominoes we could put each of + and - into. If it is equal
 * to the #left, any domino we can only put + or - in one cell of must have it. */
static int solve_countdominoes_nonneutral(game_state *state, rowcol rc, int *counts)
{
    int which, w, i, j, ndom = 0, didsth = 0, toset;

    for (which = POSITIVE, w = 0; w < 2; which = OPPOSITE(which), w++) {
        if (rc.targets[which] == -1) continue;

        for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
            if (solve_domino_count(state, rc, i, which) > 0)
                ndom++;
        }

        if ((rc.targets[which] - counts[which]) != ndom)
            continue;

        for (i = rc.i, j = 0; j < rc.n; i += rc.di, j++) {
            if (solve_domino_count(state, rc, i, which) == 1) {
                if (POSSIBLE(i, which))
                    toset = i;
                else {
                    /* paranoia, should have been checked by solve_domino_count. */
                    assert(state->common->dominoes[i] == i+rc.di);
                    assert(POSSIBLE(i+rc.di, which));
                    toset = i+rc.di;
                }
                if (solve_set(state, toset, which, "all empty dominoes need +/- [tricky]", &rc) < 0)
                    return -1;
                didsth++;
            }
        }
    }
    return didsth;
}

/* danger, evil macro. can't use the do { ... } while(0) trick because
 * the continue breaks. */
#define SOLVE_FOR_ROWCOLS(fn) \
    ret = solve_rowcols(state, fn); \
    if (ret < 0) { debug(("%s said impossible, cannot solve", #fn)); return -1; } \
    if (ret > 0) continue

static int solve_state(game_state *state, int diff)
{
    int ret;

    debug(("solve_state, difficulty %s", magnets_diffnames[diff]));

    solve_clearflags(state);
    if (solve_startflags(state) < 0) return -1;

    while (1) {
        ret = solve_force(state);
        if (ret > 0) continue;
        if (ret < 0) return -1;

        ret = solve_neither(state);
        if (ret > 0) continue;
        if (ret < 0) return -1;

        SOLVE_FOR_ROWCOLS(solve_checkfull);
        SOLVE_FOR_ROWCOLS(solve_oddlength);

        if (diff < DIFF_TRICKY) break;

        SOLVE_FOR_ROWCOLS(solve_advancedfull);
        SOLVE_FOR_ROWCOLS(solve_nonneutral);
        SOLVE_FOR_ROWCOLS(solve_countdominoes_neutral);
        SOLVE_FOR_ROWCOLS(solve_countdominoes_nonneutral);

        /* more ... */

        break;
    }
    return check_completion(state);
}


static char *game_state_diff(const game_state *src, const game_state *dst,
                             bool issolve)
{
    char *ret = NULL, buf[80], c;
    int retlen = 0, x, y, i, k;

    assert(src->w == dst->w && src->h == dst->h);

    if (issolve) {
        ret = sresize(ret, 3, char);
        ret[0] = 'S'; ret[1] = ';'; ret[2] = '\0';
        retlen += 2;
    }
    for (x = 0; x < dst->w; x++) {
        for (y = 0; y < dst->h; y++) {
            i = y*dst->w+x;

            if (src->common->dominoes[i] == i) continue;

#define APPEND do { \
    ret = sresize(ret, retlen + k + 1, char); \
    strcpy(ret + retlen, buf); \
    retlen += k; \
} while(0)

            if ((src->grid[i] != dst->grid[i]) ||
                ((src->flags[i] & GS_SET) != (dst->flags[i] & GS_SET))) {
                if (dst->grid[i] == EMPTY && !(dst->flags[i] & GS_SET))
                    c = ' ';
                else
                    c = GRID2CHAR(dst->grid[i]);
                k = sprintf(buf, "%c%d,%d;", (int)c, x, y);
                APPEND;
            }
        }
    }
    debug(("game_state_diff returns %s", ret));
    return ret;
}

static void solve_from_aux(const game_state *state, const char *aux)
{
    int i;
    assert(strlen(aux) == state->wh);
    for (i = 0; i < state->wh; i++) {
        state->grid[i] = CHAR2GRID(aux[i]);
        state->flags[i] |= GS_SET;
    }
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    game_state *solved = dup_game(currstate);
    char *move = NULL;
    int ret;

    if (aux && strlen(aux) == state->wh) {
        solve_from_aux(solved, aux);
        goto solved;
    }

    if (solve_state(solved, DIFFCOUNT) > 0) goto solved;
    free_game(solved);

    solved = dup_game(state);
    ret = solve_state(solved, DIFFCOUNT);
    if (ret > 0) goto solved;
    free_game(solved);

    *error = (ret < 0) ? "Puzzle is impossible." : "Unable to solve puzzle.";
    return NULL;

solved:
    move = game_state_diff(currstate, solved, true);
    free_game(solved);
    return move;
}

static int solve_unnumbered(game_state *state)
{
    int i, ret;
    while (1) {
        ret = solve_force(state);
        if (ret > 0) continue;
        if (ret < 0) return -1;

        ret = solve_neither(state);
        if (ret > 0) continue;
        if (ret < 0) return -1;

        break;
    }
    for (i = 0; i < state->wh; i++) {
        if (!(state->flags[i] & GS_SET)) return 0;
    }
    return 1;
}

static int lay_dominoes(game_state *state, random_state *rs, int *scratch)
{
    int n, i, ret = 0, nlaid = 0, n_initial_neutral;

    for (i = 0; i < state->wh; i++) {
        scratch[i] = i;
        state->grid[i] = EMPTY;
        state->flags[i] = (state->common->dominoes[i] == i) ? GS_SET : 0;
    }
    shuffle(scratch, state->wh, sizeof(int), rs);

    n_initial_neutral = (state->wh > 100) ? 5 : (state->wh / 10);

    for (n = 0; n < state->wh; n++) {
        /* Find a space ... */

        i = scratch[n];
        if (state->flags[i] & GS_SET) continue; /* already laid here. */

        /* ...and lay a domino if we can. */

        debug(("Laying domino at i:%d, (%d,%d)\n", i, i%state->w, i/state->w));

        /* The choice of which type of domino to lay here leads to subtle differences
         * in the sorts of boards that get produced. Too much bias towards magnets
         * leads to games that are too easy.
         *
         * Currently, it lays a small set of dominoes at random as neutral, and
         * then lays the rest preferring to be magnets -- however, if the
         * current layout is such that a magnet won't go there, then it lays
         * another neutral.
         *
         * The number of initially neutral dominoes is limited as grids get bigger:
         * too many neutral dominoes invariably ends up with insoluble puzzle at
         * this size, and the positioning process means it'll always end up laying
         * more than the initial 5 anyway.
         */

        /* We should always be able to lay a neutral anywhere. */
        assert(!(state->flags[i] & GS_NOTNEUTRAL));

        if (n < n_initial_neutral) {
            debug(("  ...laying neutral\n"));
            ret = solve_set(state, i, NEUTRAL, "layout initial neutral", NULL);
        } else {
            debug(("  ... preferring magnet\n"));
            if (!(state->flags[i] & GS_NOTPOSITIVE))
                ret = solve_set(state, i, POSITIVE, "layout", NULL);
            else if (!(state->flags[i] & GS_NOTNEGATIVE))
                ret = solve_set(state, i, NEGATIVE, "layout", NULL);
            else
                ret = solve_set(state, i, NEUTRAL, "layout", NULL);
        }
        if (!ret) {
            debug(("Unable to lay anything at (%d,%d), giving up.",
                   i%state->w, i/state->w));
            ret = -1;
            break;
        }

        nlaid++;
        ret = solve_unnumbered(state);
        if (ret == -1)
            debug(("solve_unnumbered decided impossible.\n"));
        if (ret != 0)
            break;
    }

    debug(("Laid %d dominoes, total %d dominoes.\n", nlaid, state->wh/2));
    game_debug(state, "Final layout");
    return ret;
}

static void gen_game(game_state *new, random_state *rs)
{
    int ret, x, y, val;
    int *scratch = snewn(new->wh, int);

#ifdef STANDALONE_SOLVER
    if (verbose) printf("Generating new game...\n");
#endif

    clear_state(new);
    sfree(new->common->dominoes); /* bit grotty. */
    new->common->dominoes = domino_layout(new->w, new->h, rs);

    do {
        ret = lay_dominoes(new, rs, scratch);
    } while(ret == -1);

    /* for each cell, update colcount/rowcount as appropriate. */
    memset(new->common->colcount, 0, new->w*3*sizeof(int));
    memset(new->common->rowcount, 0, new->h*3*sizeof(int));
    for (x = 0; x < new->w; x++) {
        for (y = 0; y < new->h; y++) {
            val = new->grid[y*new->w+x];
            new->common->colcount[x*3+val]++;
            new->common->rowcount[y*3+val]++;
        }
    }
    new->numbered = true;

    sfree(scratch);
}

static void generate_aux(game_state *new, char *aux)
{
    int i;
    for (i = 0; i < new->wh; i++)
        aux[i] = GRID2CHAR(new->grid[i]);
    aux[new->wh] = '\0';
}

static int check_difficulty(const game_params *params, game_state *new,
                            random_state *rs)
{
    int *scratch, *grid_correct, slen, i;

    memset(new->grid, EMPTY, new->wh*sizeof(int));

    if (params->diff > DIFF_EASY) {
        /* If this is too easy, return. */
        if (solve_state(new, params->diff-1) > 0) {
            debug(("Puzzle is too easy."));
            return -1;
        }
    }
    if (solve_state(new, params->diff) <= 0) {
        debug(("Puzzle is not soluble at requested difficulty."));
        return -1;
    }
    if (!params->stripclues) return 0;

    /* Copy the correct grid away. */
    grid_correct = snewn(new->wh, int);
    memcpy(grid_correct, new->grid, new->wh*sizeof(int));

    /* Create shuffled array of side-clue locations. */
    slen = new->w*2 + new->h*2;
    scratch = snewn(slen, int);
    for (i = 0; i < slen; i++) scratch[i] = i;
    shuffle(scratch, slen, sizeof(int), rs);

    /* For each clue, check whether removing it makes the puzzle unsoluble;
     * put it back if so. */
    for (i = 0; i < slen; i++) {
        int num = scratch[i], which, roworcol, target, targetn, ret;
        rowcol rc;

        /* work out which clue we meant. */
        if (num < new->w+new->h) { which = POSITIVE; }
        else { which = NEGATIVE; num -= new->w+new->h; }

        if (num < new->w) { roworcol = COLUMN; }
        else { roworcol = ROW; num -= new->w; }

        /* num is now the row/column index in question. */
        rc = mkrowcol(new, num, roworcol);

        /* Remove clue, storing original... */
        target = rc.targets[which];
        targetn = rc.targets[NEUTRAL];
        rc.targets[which] = -1;
        rc.targets[NEUTRAL] = -1;

        /* ...and see if we can still solve it. */
        game_debug(new, "removed clue, new board:");
        memset(new->grid, EMPTY, new->wh * sizeof(int));
        ret = solve_state(new, params->diff);
        assert(ret != -1);

        if (ret == 0 ||
            memcmp(new->grid, grid_correct, new->wh*sizeof(int)) != 0) {
            /* We made it ambiguous: put clue back. */
            debug(("...now impossible/different, put clue back."));
            rc.targets[which] = target;
            rc.targets[NEUTRAL] = targetn;
        }
    }
    sfree(scratch);
    sfree(grid_correct);

    return 0;
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux_r, bool interactive)
{
    game_state *new = new_state(params->w, params->h);
    char *desc, *aux = snewn(new->wh+1, char);

    do {
        gen_game(new, rs);
        generate_aux(new, aux);
    } while (check_difficulty(params, new, rs) < 0);

    /* now we're complete, generate the description string
     * and an aux_info for the completed game. */
    desc = generate_desc(new);

    free_game(new);

    *aux_r = aux;
    return desc;
}

struct game_ui {
    int cur_x, cur_y;
    bool cur_visible;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->cur_x = ui->cur_y = 0;
    ui->cur_visible = getenv_bool("PUZZLES_SHOW_CURSOR", false);
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
    if (!oldstate->completed && newstate->completed)
        ui->cur_visible = false;
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    int idx;

    if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) return "";
        idx = ui->cur_y * state->w + ui->cur_x;
        if (button == CURSOR_SELECT) {
            if (state->grid[idx] == NEUTRAL && state->flags[idx] & GS_SET)
                return "";
            switch (state->grid[idx]) {
              case EMPTY: return "+";
              case POSITIVE: return "-";
              case NEGATIVE: return "Clear";
            }
        }
        if (button == CURSOR_SELECT2) {
            if (state->grid[idx] != NEUTRAL) return "";
            if (state->flags[idx] & GS_SET) /* neutral */
                return "?";
            if (state->flags[idx] & GS_NOTNEUTRAL) /* !neutral */
                return "Clear";
            else
                return "X";
        }
    }
    return "";
}
    
struct game_drawstate {
    int tilesize;
    bool started, solved;
    int w, h;
    unsigned long *what;                /* size w*h */
    unsigned long *colwhat, *rowwhat;   /* size 3*w, 3*h */
};

#define DS_WHICH_MASK 0xf

#define DS_ERROR    0x10
#define DS_CURSOR   0x20
#define DS_SET      0x40
#define DS_NOTPOS   0x80
#define DS_NOTNEG   0x100
#define DS_NOTNEU   0x200
#define DS_FLASH    0x400

#define PREFERRED_TILE_SIZE 32
#define TILE_SIZE (ds->tilesize)
#define BORDER    (TILE_SIZE / 8)

#define COORD(x) ( (x+1) * TILE_SIZE + BORDER )
#define FROMCOORD(x) ( (x - BORDER) / TILE_SIZE - 1 )

static bool is_clue(const game_state *state, int x, int y)
{
    int h = state->h, w = state->w;

    if (((x == -1 || x == w) && y >= 0 && y < h) ||
        ((y == -1 || y == h) && x >= 0 && x < w))
        return true;

    return false;
}

static int clue_index(const game_state *state, int x, int y)
{
    int h = state->h, w = state->w;

    if (y == -1)
        return x;
    else if (x == w)
        return w + y;
    else if (y == h)
        return 2 * w + h - x - 1;
    else if (x == -1)
        return 2 * (w + h) - y - 1;

    return -1;
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int gx = FROMCOORD(x), gy = FROMCOORD(y), idx, curr;
    char *nullret = NULL, buf[80], movech;
    enum { CYCLE_MAGNET, CYCLE_NEUTRAL } action;

    if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->cur_x, &ui->cur_y, state->w, state->h, false);
        ui->cur_visible = true;
        return UI_UPDATE;
    } else if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) {
            ui->cur_visible = true;
            return UI_UPDATE;
        }
        action = (button == CURSOR_SELECT) ? CYCLE_MAGNET : CYCLE_NEUTRAL;
        gx = ui->cur_x;
        gy = ui->cur_y;
    } else if (INGRID(state, gx, gy) &&
               (button == LEFT_BUTTON || button == RIGHT_BUTTON)) {
        if (ui->cur_visible) {
            ui->cur_visible = false;
            nullret = UI_UPDATE;
        }
        action = (button == LEFT_BUTTON) ? CYCLE_MAGNET : CYCLE_NEUTRAL;
    } else if (button == LEFT_BUTTON && is_clue(state, gx, gy)) {
        sprintf(buf, "D%d,%d", gx, gy);
        return dupstr(buf);
    } else
        return NULL;

    idx = gy * state->w + gx;
    if (state->common->dominoes[idx] == idx) return nullret;
    curr = state->grid[idx];

    if (action == CYCLE_MAGNET) {
        /* ... empty --> positive --> negative --> empty ... */

        if (state->grid[idx] == NEUTRAL && state->flags[idx] & GS_SET)
            return nullret; /* can't cycle a magnet from a neutral. */
        movech = (curr == EMPTY) ? '+' : (curr == POSITIVE) ? '-' : ' ';
    } else if (action == CYCLE_NEUTRAL) {
        /* ... empty -> neutral -> !neutral --> empty ... */

        if (state->grid[idx] != NEUTRAL)
            return nullret; /* can't cycle through neutral from a magnet. */

        /* All of these are grid == EMPTY == NEUTRAL; it twiddles
         * combinations of flags. */
        if (state->flags[idx] & GS_SET) /* neutral */
            movech = '?';
        else if (state->flags[idx] & GS_NOTNEUTRAL) /* !neutral */
            movech = ' ';
        else
            movech = '.';
    } else {
        assert(!"unknown action");
	movech = 0;		       /* placate optimiser */
    }

    sprintf(buf, "%c%d,%d", movech, gx, gy);

    return dupstr(buf);
}

static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *ret = dup_game(state);
    int x, y, n, idx, idx2;
    char c;

    if (!*move) goto badmove;
    while (*move) {
        c = *move++;
        if (c == 'S') {
            ret->solved = true;
            n = 0;
        } else if (c == '+' || c == '-' ||
                   c == '.' || c == ' ' || c == '?') {
            if ((sscanf(move, "%d,%d%n", &x, &y, &n) != 2) ||
                !INGRID(state, x, y)) goto badmove;

            idx = y*state->w + x;
            idx2 = state->common->dominoes[idx];
            if (idx == idx2) goto badmove;

            ret->flags[idx] &= ~GS_NOTMASK;
            ret->flags[idx2] &= ~GS_NOTMASK;

            if (c == ' ' || c == '?') {
                ret->grid[idx] = EMPTY;
                ret->grid[idx2] = EMPTY;
                ret->flags[idx] &= ~GS_SET;
                ret->flags[idx2] &= ~GS_SET;
                if (c == '?') {
                    ret->flags[idx] |= GS_NOTNEUTRAL;
                    ret->flags[idx2] |= GS_NOTNEUTRAL;
                }
            } else {
                ret->grid[idx] = CHAR2GRID(c);
                ret->grid[idx2] = OPPOSITE(CHAR2GRID(c));
                ret->flags[idx] |= GS_SET;
                ret->flags[idx2] |= GS_SET;
            }
        } else if (c == 'D' && sscanf(move, "%d,%d%n", &x, &y, &n) == 2 &&
                   is_clue(ret, x, y)) {
            ret->counts_done[clue_index(ret, x, y)] ^= 1;
        } else
            goto badmove;

        move += n;
        if (*move == ';') move++;
        else if (*move) goto badmove;
    }
    if (check_completion(ret) == 1)
        ret->completed = true;

    return ret;

badmove:
    free_game(ret);
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

    *x = TILE_SIZE * (params->w+2) + 2 * BORDER;
    *y = TILE_SIZE * (params->h+2) + 2 * BORDER;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    game_mkhighlight(fe, ret, COL_BACKGROUND, COL_HIGHLIGHT, COL_LOWLIGHT);

    for (i = 0; i < 3; i++) {
        ret[COL_TEXT * 3 + i] = 0.0F;
        ret[COL_NEGATIVE * 3 + i] = 0.0F;
        ret[COL_CURSOR * 3 + i] = 0.9F;
        ret[COL_DONE * 3 + i] = ret[COL_BACKGROUND * 3 + i] / 1.5F;
    }

    ret[COL_POSITIVE * 3 + 0] = 0.8F;
    ret[COL_POSITIVE * 3 + 1] = 0.0F;
    ret[COL_POSITIVE * 3 + 2] = 0.0F;

    ret[COL_NEUTRAL * 3 + 0] = 0.10F;
    ret[COL_NEUTRAL * 3 + 1] = 0.60F;
    ret[COL_NEUTRAL * 3 + 2] = 0.10F;

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_NOT * 3 + 0] = 0.2F;
    ret[COL_NOT * 3 + 1] = 0.2F;
    ret[COL_NOT * 3 + 2] = 1.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->started = false;
    ds->solved = false;
    ds->w = state->w;
    ds->h = state->h;

    ds->what = snewn(state->wh, unsigned long);
    memset(ds->what, 0, state->wh*sizeof(unsigned long));

    ds->colwhat = snewn(state->w*3, unsigned long);
    memset(ds->colwhat, 0, state->w*3*sizeof(unsigned long));
    ds->rowwhat = snewn(state->h*3, unsigned long);
    memset(ds->rowwhat, 0, state->h*3*sizeof(unsigned long));

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->colwhat);
    sfree(ds->rowwhat);
    sfree(ds->what);
    sfree(ds);
}

static void draw_num(drawing *dr, game_drawstate *ds, int rowcol, int which,
                         int idx, int colbg, int col, int num)
{
    char buf[32];
    int cx, cy, tsz;

    if (num < 0) return;

    sprintf(buf, "%d", num);
    tsz = (strlen(buf) == 1) ? (7*TILE_SIZE/10) : (9*TILE_SIZE/10)/strlen(buf);

    if (rowcol == ROW) {
        cx = BORDER;
        if (which == NEGATIVE) cx += TILE_SIZE * (ds->w+1);
        cy = BORDER + TILE_SIZE * (idx+1);
    } else {
        cx = BORDER + TILE_SIZE * (idx+1);
        cy = BORDER;
        if (which == NEGATIVE) cy += TILE_SIZE * (ds->h+1);
    }

    draw_rect(dr, cx, cy, TILE_SIZE, TILE_SIZE, colbg);
    draw_text(dr, cx + TILE_SIZE/2, cy + TILE_SIZE/2, FONT_VARIABLE, tsz,
              ALIGN_VCENTRE | ALIGN_HCENTRE, col, buf);

    draw_update(dr, cx, cy, TILE_SIZE, TILE_SIZE);
}

static void draw_sym(drawing *dr, game_drawstate *ds, int x, int y, int which, int col)
{
    int cx = COORD(x), cy = COORD(y);
    int ccx = cx + TILE_SIZE/2, ccy = cy + TILE_SIZE/2;
    int roff = TILE_SIZE/4, rsz = 2*roff+1;
    int soff = TILE_SIZE/16, ssz = 2*soff+1;

    if (which == POSITIVE || which == NEGATIVE) {
        draw_rect(dr, ccx - roff, ccy - soff, rsz, ssz, col);
        if (which == POSITIVE)
            draw_rect(dr, ccx - soff, ccy - roff, ssz, rsz, col);
    } else if (col == COL_NOT) {
        /* not-a-neutral is a blue question mark. */
        char qu[2] = { '?', 0 };
        draw_text(dr, ccx, ccy, FONT_VARIABLE, 7*TILE_SIZE/10,
                  ALIGN_VCENTRE | ALIGN_HCENTRE, col, qu);
    } else {
        draw_line(dr, ccx - roff, ccy - roff, ccx + roff, ccy + roff, col);
        draw_line(dr, ccx + roff, ccy - roff, ccx - roff, ccy + roff, col);
    }
}

enum {
    TYPE_L,
    TYPE_R,
    TYPE_T,
    TYPE_B,
    TYPE_BLANK
};

/* NOT responsible for redrawing background or updating. */
static void draw_tile_col(drawing *dr, game_drawstate *ds, int *dominoes,
                          int x, int y, int which, int bg, int fg, int perc)
{
    int cx = COORD(x), cy = COORD(y), i, other, type = TYPE_BLANK;
    int gutter, radius, coffset;

    /* gutter is TSZ/16 for 100%, 8*TSZ/16 (TSZ/2) for 0% */
    gutter = (TILE_SIZE / 16) + ((100 - perc) * (7*TILE_SIZE / 16))/100;
    radius = (perc * (TILE_SIZE / 8)) / 100;
    coffset = gutter + radius;

    i = y*ds->w + x;
    other = dominoes[i];

    if (other == i) return;
    else if (other == i+1) type = TYPE_L;
    else if (other == i-1) type = TYPE_R;
    else if (other == i+ds->w) type = TYPE_T;
    else if (other == i-ds->w) type = TYPE_B;
    else assert(!"mad domino orientation");

    /* domino drawing shamelessly stolen from dominosa.c. */
    if (type == TYPE_L || type == TYPE_T)
        draw_circle(dr, cx+coffset, cy+coffset,
                    radius, bg, bg);
    if (type == TYPE_R || type == TYPE_T)
        draw_circle(dr, cx+TILE_SIZE-1-coffset, cy+coffset,
                    radius, bg, bg);
    if (type == TYPE_L || type == TYPE_B)
        draw_circle(dr, cx+coffset, cy+TILE_SIZE-1-coffset,
                    radius, bg, bg);
    if (type == TYPE_R || type == TYPE_B)
        draw_circle(dr, cx+TILE_SIZE-1-coffset,
                    cy+TILE_SIZE-1-coffset,
                    radius, bg, bg);

    for (i = 0; i < 2; i++) {
        int x1, y1, x2, y2;

        x1 = cx + (i ? gutter : coffset);
        y1 = cy + (i ? coffset : gutter);
        x2 = cx + TILE_SIZE-1 - (i ? gutter : coffset);
        y2 = cy + TILE_SIZE-1 - (i ? coffset : gutter);
        if (type == TYPE_L)
            x2 = cx + TILE_SIZE;
        else if (type == TYPE_R)
            x1 = cx;
        else if (type == TYPE_T)
            y2 = cy + TILE_SIZE ;
        else if (type == TYPE_B)
            y1 = cy;

        draw_rect(dr, x1, y1, x2-x1+1, y2-y1+1, bg);
    }

    if (fg != -1) draw_sym(dr, ds, x, y, which, fg);
}

static void draw_tile(drawing *dr, game_drawstate *ds, int *dominoes,
                      int x, int y, unsigned long flags)
{
    int cx = COORD(x), cy = COORD(y), bg, fg, perc = 100;
    int which = flags & DS_WHICH_MASK;

    flags &= ~DS_WHICH_MASK;

    draw_rect(dr, cx, cy, TILE_SIZE, TILE_SIZE, COL_BACKGROUND);

    if (flags & DS_CURSOR)
        bg = COL_CURSOR;        /* off-white white for cursor */
    else if (which == POSITIVE)
        bg = COL_POSITIVE;
    else if (which == NEGATIVE)
        bg = COL_NEGATIVE;
    else if (flags & DS_SET)
        bg = COL_NEUTRAL;       /* green inner for neutral cells */
    else
        bg = COL_LOWLIGHT;      /* light grey for empty cells. */

    if (which == EMPTY && !(flags & DS_SET)) {
        int notwhich = -1;
        fg = -1; /* don't draw cross unless actually set as neutral. */

        if (flags & DS_NOTPOS) notwhich = POSITIVE;
        if (flags & DS_NOTNEG) notwhich = NEGATIVE;
        if (flags & DS_NOTNEU) notwhich = NEUTRAL;
        if (notwhich != -1) {
            which = notwhich;
            fg = COL_NOT;
        }
    } else
        fg = (flags & DS_ERROR) ? COL_ERROR :
             (flags & DS_CURSOR) ? COL_TEXT : COL_BACKGROUND;

    draw_rect(dr, cx, cy, TILE_SIZE, TILE_SIZE, COL_BACKGROUND);

    if (flags & DS_FLASH) {
        int bordercol = COL_HIGHLIGHT;
        draw_tile_col(dr, ds, dominoes, x, y, which, bordercol, -1, perc);
        perc = 3*perc/4;
    }
    draw_tile_col(dr, ds, dominoes, x, y, which, bg, fg, perc);

    draw_update(dr, cx, cy, TILE_SIZE, TILE_SIZE);
}

static int get_count_color(const game_state *state, int rowcol, int which,
                           int index, int target)
{
    int idx;
    int count = count_rowcol(state, index, rowcol, which);

    if ((count > target) ||
        (count < target && !count_rowcol(state, index, rowcol, -1))) {
        return COL_ERROR;
    } else if (rowcol == COLUMN) {
        idx = clue_index(state, index, which == POSITIVE ? -1 : state->h);
    } else {
        idx = clue_index(state, which == POSITIVE ? -1 : state->w, index);
    }

    if (state->counts_done[idx]) {
        return COL_DONE;
    }

    return COL_TEXT;
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int x, y, w = state->w, h = state->h, which, i, j;
    bool flash;

    flash = (int)(flashtime * 5 / FLASH_TIME) % 2;

    if (!ds->started) {
        /* draw corner +-. */
        draw_sym(dr, ds, -1, -1, POSITIVE, COL_TEXT);
        draw_sym(dr, ds, state->w, state->h, NEGATIVE, COL_TEXT);

        draw_update(dr, 0, 0,
                    TILE_SIZE * (ds->w+2) + 2 * BORDER,
                    TILE_SIZE * (ds->h+2) + 2 * BORDER);
    }

    /* Draw grid */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int idx = y*w+x;
            unsigned long c = state->grid[idx];

            if (state->flags[idx] & GS_ERROR)
                c |= DS_ERROR;
            if (state->flags[idx] & GS_SET)
                c |= DS_SET;

            if (x == ui->cur_x && y == ui->cur_y && ui->cur_visible)
                c |= DS_CURSOR;

            if (flash)
                c |= DS_FLASH;

            if (state->flags[idx] & GS_NOTPOSITIVE)
                c |= DS_NOTPOS;
            if (state->flags[idx] & GS_NOTNEGATIVE)
                c |= DS_NOTNEG;
            if (state->flags[idx] & GS_NOTNEUTRAL)
                c |= DS_NOTNEU;

            if (ds->what[idx] != c || !ds->started) {
                draw_tile(dr, ds, state->common->dominoes, x, y, c);
                ds->what[idx] = c;
            }
        }
    }
    /* Draw counts around side */
    for (which = POSITIVE, j = 0; j < 2; which = OPPOSITE(which), j++) {
        for (i = 0; i < w; i++) {
            int index = i * 3 + which;
            int target = state->common->colcount[index];
            int color = get_count_color(state, COLUMN, which, i, target);

            if (color != ds->colwhat[index] || !ds->started) {
                draw_num(dr, ds, COLUMN, which, i, COL_BACKGROUND, color, target);
                ds->colwhat[index] = color;
            }
        }
        for (i = 0; i < h; i++) {
            int index = i * 3 + which;
            int target = state->common->rowcount[index];
            int color = get_count_color(state, ROW, which, i, target);

            if (color != ds->rowwhat[index] || !ds->started) {
                draw_num(dr, ds, ROW, which, i, COL_BACKGROUND, color, target);
                ds->rowwhat[index] = color;
            }
        }
    }

    ds->started = true;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
        !oldstate->solved && !newstate->solved)
        return FLASH_TIME;
    return 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->cur_visible) {
        *x = COORD(ui->cur_x);
        *y = COORD(ui->cur_y);
        *w = *h = TILE_SIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->completed ? +1 : 0;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * I'll use 6mm squares by default.
     */
    game_compute_size(params, 600, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
    int w = state->w, h = state->h;
    int ink = print_mono_colour(dr, 0);
    int paper = print_mono_colour(dr, 1);
    int x, y, which, i, j;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);
    ds->w = w; ds->h = h;

    /* Border. */
    print_line_width(dr, TILE_SIZE/12);

    /* Numbers and +/- for corners. */
    draw_sym(dr, ds, -1, -1, POSITIVE, ink);
    draw_sym(dr, ds, state->w, state->h, NEGATIVE, ink);
    for (which = POSITIVE, j = 0; j < 2; which = OPPOSITE(which), j++) {
        for (i = 0; i < w; i++) {
            draw_num(dr, ds, COLUMN, which, i, paper, ink,
                         state->common->colcount[i*3+which]);
        }
        for (i = 0; i < h; i++) {
            draw_num(dr, ds, ROW, which, i, paper, ink,
                         state->common->rowcount[i*3+which]);
        }
    }

    /* Dominoes. */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            i = y*state->w + x;
	    if (state->common->dominoes[i] == i+1 ||
		state->common->dominoes[i] == i+w) {
		int dx = state->common->dominoes[i] == i+1 ? 2 : 1;
		int dy = 3 - dx;
		int xx, yy;
		int cx = COORD(x), cy = COORD(y);

		print_line_width(dr, 0);

		/* Ink the domino */
		for (yy = 0; yy < 2; yy++)
		    for (xx = 0; xx < 2; xx++)
			draw_circle(dr,
				    cx+xx*dx*TILE_SIZE+(1-2*xx)*3*TILE_SIZE/16,
				    cy+yy*dy*TILE_SIZE+(1-2*yy)*3*TILE_SIZE/16,
				    TILE_SIZE/8, ink, ink);
		draw_rect(dr, cx + TILE_SIZE/16, cy + 3*TILE_SIZE/16,
			  dx*TILE_SIZE - 2*(TILE_SIZE/16),
			  dy*TILE_SIZE - 6*(TILE_SIZE/16), ink);
		draw_rect(dr, cx + 3*TILE_SIZE/16, cy + TILE_SIZE/16,
			  dx*TILE_SIZE - 6*(TILE_SIZE/16),
			  dy*TILE_SIZE - 2*(TILE_SIZE/16), ink);

		/* Un-ink the domino interior */
		for (yy = 0; yy < 2; yy++)
		    for (xx = 0; xx < 2; xx++)
			draw_circle(dr,
				    cx+xx*dx*TILE_SIZE+(1-2*xx)*3*TILE_SIZE/16,
				    cy+yy*dy*TILE_SIZE+(1-2*yy)*3*TILE_SIZE/16,
				    3*TILE_SIZE/32, paper, paper);
		draw_rect(dr, cx + 3*TILE_SIZE/32, cy + 3*TILE_SIZE/16,
			  dx*TILE_SIZE - 2*(3*TILE_SIZE/32),
			  dy*TILE_SIZE - 6*(TILE_SIZE/16), paper);
		draw_rect(dr, cx + 3*TILE_SIZE/16, cy + 3*TILE_SIZE/32,
			  dx*TILE_SIZE - 6*(TILE_SIZE/16),
			  dy*TILE_SIZE - 2*(3*TILE_SIZE/32), paper);
	    }
        }
    }

    /* Grid symbols (solution). */
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            i = y*state->w + x;
	    if ((state->grid[i] != NEUTRAL) || (state->flags[i] & GS_SET))
		draw_sym(dr, ds, x, y, state->grid[i], ink);
	}
    }
}

#ifdef COMBINED
#define thegame magnets
#endif

const struct game thegame = {
    "Magnets", "games.magnets", "magnets",
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
    PREFERRED_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_get_cursor_location,
    game_status,
    true, false, game_print_size, game_print,
    false,			       /* wants_statusbar */
    false, NULL,                       /* timing_state */
    REQUIRE_RBUTTON,		       /* flags */
};

#ifdef STANDALONE_SOLVER

#include <time.h>
#include <stdarg.h>

static const char *quis = NULL;
static bool csv = false;

static void usage(FILE *out) {
    fprintf(out, "usage: %s [-v] [--print] <params>|<game id>\n", quis);
}

static void doprint(game_state *state)
{
    char *fmt = game_text_format(state);
    printf("%s", fmt);
    sfree(fmt);
}

static void pnum(int n, int ntot, const char *desc)
{
    printf("%2.1f%% (%d) %s", (double)n*100.0 / (double)ntot, n, desc);
}

static void start_soak(game_params *p, random_state *rs)
{
    time_t tt_start, tt_now, tt_last;
    char *aux;
    game_state *s, *s2;
    int n = 0, nsolved = 0, nimpossible = 0, ntricky = 0, ret, i;
    long nn, nn_total = 0, nn_solved = 0, nn_tricky = 0;

    tt_start = tt_now = time(NULL);

    if (csv)
        printf("time, w, h,  #generated, #solved, #tricky, #impossible,  "
               "#neutral, #neutral/solved, #neutral/tricky\n");
    else
        printf("Soak-testing a %dx%d grid.\n", p->w, p->h);

    s = new_state(p->w, p->h);
    aux = snewn(s->wh+1, char);

    while (1) {
        gen_game(s, rs);

        nn = 0;
        for (i = 0; i < s->wh; i++) {
            if (s->grid[i] == NEUTRAL) nn++;
        }

        generate_aux(s, aux);
        memset(s->grid, EMPTY, s->wh * sizeof(int));
        s2 = dup_game(s);

        ret = solve_state(s, DIFFCOUNT);

        n++;
        nn_total += nn;
        if (ret > 0) {
            nsolved++;
            nn_solved += nn;
            if (solve_state(s2, DIFF_EASY) <= 0) {
                ntricky++;
                nn_tricky += nn;
            }
        } else if (ret < 0) {
            char *desc = generate_desc(s);
            solve_from_aux(s, aux);
            printf("Game considered impossible:\n  %dx%d:%s\n",
                    p->w, p->h, desc);
            sfree(desc);
            doprint(s);
            nimpossible++;
        }

        free_game(s2);

        tt_last = time(NULL);
        if (tt_last > tt_now) {
            tt_now = tt_last;
            if (csv) {
                printf("%d,%d,%d, %d,%d,%d,%d, %ld,%ld,%ld\n",
                       (int)(tt_now - tt_start), p->w, p->h,
                       n, nsolved, ntricky, nimpossible,
                       nn_total, nn_solved, nn_tricky);
            } else {
                printf("%d total, %3.1f/s, ",
                       n, (double)n / ((double)tt_now - tt_start));
                pnum(nsolved, n, "solved"); printf(", ");
                pnum(ntricky, n, "tricky");
                if (nimpossible > 0)
                    pnum(nimpossible, n, "impossible");
                printf("\n");

                printf("  overall %3.1f%% neutral (%3.1f%% for solved, %3.1f%% for tricky)\n",
                       (double)(nn_total * 100) / (double)(p->w * p->h * n),
                       (double)(nn_solved * 100) / (double)(p->w * p->h * nsolved),
                       (double)(nn_tricky * 100) / (double)(p->w * p->h * ntricky));
            }
        }
    }
    free_game(s);
    sfree(aux);
}

int main(int argc, char *argv[])
{
    bool print = false, soak = false, solved = false;
    int ret;
    char *id = NULL, *desc, *desc_gen = NULL, *aux = NULL;
    const char *err;
    game_state *s = NULL;
    game_params *p = NULL;
    random_state *rs = NULL;
    time_t seed = time(NULL);

    setvbuf(stdout, NULL, _IONBF, 0);

    quis = argv[0];
    while (--argc > 0) {
        char *p = (char*)(*++argv);
        if (!strcmp(p, "-v") || !strcmp(p, "--verbose")) {
            verbose = true;
        } else if (!strcmp(p, "--csv")) {
            csv = true;
        } else if (!strcmp(p, "-e") || !strcmp(p, "--seed")) {
            seed = atoi(*++argv);
            argc--;
        } else if (!strcmp(p, "-p") || !strcmp(p, "--print")) {
            print = true;
        } else if (!strcmp(p, "-s") || !strcmp(p, "--soak")) {
            soak = true;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            usage(stderr);
            exit(1);
        } else {
            id = p;
        }
    }

    rs = random_new((void*)&seed, sizeof(time_t));

    if (!id) {
        fprintf(stderr, "usage: %s [-v] [--soak] <params> | <game_id>\n", argv[0]);
        goto done;
    }
    desc = strchr(id, ':');
    if (desc) *desc++ = '\0';

    p = default_params();
    decode_params(p, id);
    err = validate_params(p, true);
    if (err) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        goto done;
    }

    if (soak) {
        if (desc) {
            fprintf(stderr, "%s: --soak needs parameters, not description.\n", quis);
            goto done;
        }
        start_soak(p, rs);
        goto done;
    }

    if (!desc)
        desc = desc_gen = new_game_desc(p, rs, &aux, false);

    err = validate_desc(p, desc);
    if (err) {
        fprintf(stderr, "%s: %s\nDescription: %s\n", quis, err, desc);
        goto done;
    }
    s = new_game(NULL, p, desc);
    printf("%s:%s (seed %ld)\n", id, desc, (long)seed);
    if (aux) {
        /* We just generated this ourself. */
        if (verbose || print) {
            doprint(s);
            solve_from_aux(s, aux);
            solved = true;
        }
    } else {
        doprint(s);
        verbose = true;
        ret = solve_state(s, DIFFCOUNT);
        if (ret < 0) printf("Puzzle is impossible.\n");
        else if (ret == 0) printf("Puzzle is ambiguous.\n");
        else printf("Puzzle was solved.\n");
        verbose = false;
        solved = true;
    }
    if (solved) doprint(s);

done:
    if (desc_gen) sfree(desc_gen);
    if (p) free_params(p);
    if (s) free_game(s);
    if (rs) random_free(rs);
    if (aux) sfree(aux);

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
