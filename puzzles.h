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
    RIGHT_BUTTON,
    CURSOR_UP,
    CURSOR_DOWN,
    CURSOR_LEFT,
    CURSOR_RIGHT,
    CURSOR_UP_LEFT,
    CURSOR_DOWN_LEFT,
    CURSOR_UP_RIGHT,
    CURSOR_DOWN_RIGHT
};

#define IGNOREARG(x) ( (x) = (x) )

typedef struct frontend frontend;
typedef struct midend_data midend_data;
typedef struct random_state random_state;
typedef struct game_params game_params;
typedef struct game_state game_state;
typedef struct game_drawstate game_drawstate;

/*
 * Platform routines
 */
void fatal(char *fmt, ...);
void frontend_default_colour(frontend *fe, float *output);
void draw_rect(frontend *fe, int x, int y, int w, int h, int colour);
void draw_line(frontend *fe, int x1, int y1, int x2, int y2, int colour);
void draw_polygon(frontend *fe, int *coords, int npoints,
                  int fill, int colour);
void start_draw(frontend *fe);
void draw_update(frontend *fe, int x, int y, int w, int h);
void end_draw(frontend *fe);
void deactivate_timer(frontend *fe);
void activate_timer(frontend *fe);

/*
 * midend.c
 */
midend_data *midend_new(frontend *fe);
void midend_free(midend_data *me);
void midend_set_params(midend_data *me, game_params *params);
void midend_size(midend_data *me, int *x, int *y);
void midend_new_game(midend_data *me, char *seed);
void midend_restart_game(midend_data *me);
int midend_process_key(midend_data *me, int x, int y, int button);
void midend_redraw(midend_data *me);
float *midend_colours(midend_data *me, int *ncolours);
void midend_timer(midend_data *me, float tplus);
int midend_num_presets(midend_data *me);
void midend_fetch_preset(midend_data *me, int n,
                         char **name, game_params **params);

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
    ( (type *) srealloc ((array), (number) * sizeof (type)) )

/*
 * random.c
 */
random_state *random_init(char *seed, int len);
unsigned long random_upto(random_state *state, unsigned long limit);
void random_free(random_state *state);

/*
 * Game-specific routines
 */
game_params *default_params(void);
int game_fetch_preset(int i, char **name, game_params **params);
void free_params(game_params *params);
game_params *dup_params(game_params *params);
char *new_game_seed(game_params *params);
game_state *new_game(game_params *params, char *seed);
game_state *dup_game(game_state *state);
void free_game(game_state *state);
game_state *make_move(game_state *from, int x, int y, int button);
void game_size(game_params *params, int *x, int *y);
float *game_colours(frontend *fe, game_state *state, int *ncolours);
game_drawstate *game_new_drawstate(game_state *state);
void game_free_drawstate(game_drawstate *ds);
void game_redraw(frontend *fe, game_drawstate *ds, game_state *oldstate,
                 game_state *newstate, float t);
float game_anim_length(game_state *oldstate, game_state *newstate);

#endif /* PUZZLES_PUZZLES_H */
