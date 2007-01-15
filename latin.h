#ifndef LATIN_H
#define LATIN_H

#include "puzzles.h"

typedef unsigned char digit;

/* --- Solver structures, definitions --- */

#ifdef STANDALONE_SOLVER
int solver_show_working, solver_recurse_depth;
#endif

struct latin_solver {
  int o;                /* order of latin square */
  unsigned char *cube;  /* o^3, indexed by x, y, and digit:
                           TRUE in that position indicates a possibility */
  digit *grid;          /* o^2, indexed by x and y: for final deductions */

  unsigned char *row;   /* o^2: row[y*cr+n-1] TRUE if n is in row y */
  unsigned char *col;   /* o^2: col[x*cr+n-1] TRUE if n is in col x */
};
#define cubepos(x,y,n) (((x)*solver->o+(y))*solver->o+(n)-1)
#define cube(x,y,n) (solver->cube[cubepos(x,y,n)])

#define gridpos(x,y) ((y)*solver->o+(x))
#define grid(x,y) (solver->grid[gridpos(x,y)])

/* A solo solver using this code would need these defined. See solo.c. */
#ifndef YTRANS
#define YTRANS(y) (y)
#endif
#ifndef YUNTRANS
#define YUNTRANS(y) (y)
#endif


/* --- Solver individual strategies --- */

/* Place a value at a specific location. */
void latin_solver_place(struct latin_solver *solver, int x, int y, int n);

/* Positional elimination. */
int latin_solver_elim(struct latin_solver *solver, int start, int step
#ifdef STANDALONE_SOLVER
                      , char *fmt, ...
#endif
                      );

struct latin_solver_scratch; /* private to latin.c */
/* Set elimination */
int latin_solver_set(struct latin_solver *solver,
                     struct latin_solver_scratch *scratch,
                     int start, int step1, int step2
#ifdef STANDALONE_SOLVER
                     , char *fmt, ...
#endif
                     );

/* Forcing chains */
int latin_solver_forcing(struct latin_solver *solver,
                         struct latin_solver_scratch *scratch);


/* --- Solver allocation --- */

/* Fills in (and allocates members for) a latin_solver struct.
 * Will allocate members of snew, but not snew itself
 * (allowing 'struct latin_solver' to be the first element in a larger
 * struct, for example). */
void latin_solver_alloc(struct latin_solver *solver, digit *grid, int o);
void latin_solver_free(struct latin_solver *solver);

/* Allocates scratch space (for _set and _forcing) */
struct latin_solver_scratch *
  latin_solver_new_scratch(struct latin_solver *solver);
void latin_solver_free_scratch(struct latin_solver_scratch *scratch);


/* --- Solver guts --- */

/* Looped positional elimination */
int latin_solver_diff_simple(struct latin_solver *solver);

/* Looped set elimination; *extreme is set if it used
 * the more difficult single-number elimination. */
int latin_solver_diff_set(struct latin_solver *solver,
                          struct latin_solver_scratch *scratch,
                          int extreme);

typedef int (latin_solver_callback)(digit *, int, int, void*);
/* Use to provide a standard way of dealing with solvers which can recurse;
 * pass in your enumeration for 'recursive diff' and your solver
 * callback. Returns #solutions (0 == already solved). */
int latin_solver_recurse(struct latin_solver *solver, int recdiff,
                         latin_solver_callback cb, void *ctx);

/* Individual puzzles should use their enumerations for their
 * own difficulty levels, ensuring they don't clash with these. */
enum { diff_impossible = 10, diff_ambiguous, diff_unfinished };
int latin_solver(digit *grid, int order, int maxdiff, void *unused);

void latin_solver_debug(unsigned char *cube, int o);

/* --- Generation and checking --- */

digit *latin_generate_quick(int o, random_state *rs);
digit *latin_generate(int o, random_state *rs);

int latin_check(digit *sq, int order); /* !0 => not a latin square */

void latin_debug(digit *sq, int order);

#endif
