/*
 * puzzles.h: header file for my puzzle collection
 */

#ifndef PUZZLES_PUZZLES_H
#define PUZZLES_PUZZLES_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define lenof(array) ( sizeof(array) / sizeof(*(array)) )

enum {
    LEFT_BUTTON = 0x1000,
    MIDDLE_BUTTON,
    RIGHT_BUTTON
};

/*
 * Platform routines
 */
void fatal(char *fmt, ...);

/*
 * malloc.c
 */
void *smalloc(int size);
void *srealloc(void *p, int size);
void sfree(void *p);
char *dupstr(char *s);
#define snew(type) \
    ( (type *) smalloc (sizeof (type)) )
#define snewn(number, type) \
    ( (type *) smalloc ((number) * sizeof (type)) )
#define sresize(array, number, type) \
    ( (type *) srealloc ((array), (len) * sizeof (type)) )

/*
 * random.c
 */
typedef struct random_state random_state;
random_state *random_init(char *seed, int len);
unsigned long random_upto(random_state *state, unsigned long limit);
void random_free(random_state *state);

/*
 * Game-specific routines
 */
typedef struct game_params game_params;
typedef struct game_state game_state;
char *new_game_seed(game_params *params);
game_state *new_game(game_params *params, char *seed);
game_state *dup_game(game_state *state);
void free_game(game_state *state);
game_state *make_move(game_state *from, int x, int y, int button);

#endif /* PUZZLES_PUZZLES_H */
