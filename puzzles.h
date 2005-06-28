/*
 * puzzles.h: header file for my puzzle collection
 */

#ifndef PUZZLES_PUZZLES_H
#define PUZZLES_PUZZLES_H

#include <stdlib.h> /* for size_t */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define PI 3.141592653589793238462643383279502884197169399

#define lenof(array) ( sizeof(array) / sizeof(*(array)) )

#define STR_INT(x) #x
#define STR(x) STR_INT(x)

/* NB not perfect because they evaluate arguments multiple times. */
#ifndef max
#define max(x,y) ( (x)>(y) ? (x) : (y) )
#endif /* max */
#ifndef min
#define min(x,y) ( (x)<(y) ? (x) : (y) )
#endif /* min */

enum {
    LEFT_BUTTON = 0x0200,
    MIDDLE_BUTTON,
    RIGHT_BUTTON,
    LEFT_DRAG,
    MIDDLE_DRAG,
    RIGHT_DRAG,
    LEFT_RELEASE,
    MIDDLE_RELEASE,
    RIGHT_RELEASE,
    CURSOR_UP,
    CURSOR_DOWN,
    CURSOR_LEFT,
    CURSOR_RIGHT,
    CURSOR_SELECT,
    
    /* made smaller because of 'limited range of datatype' errors. */
    MOD_CTRL       = 0x1000,
    MOD_SHFT       = 0x2000,
    MOD_NUM_KEYPAD = 0x4000,
    MOD_MASK       = 0x7000 /* mask for all modifiers */
};

#define IS_MOUSE_DOWN(m) ( (unsigned)((m) - LEFT_BUTTON) <= \
                               (unsigned)(RIGHT_BUTTON - LEFT_BUTTON))
#define IS_MOUSE_DRAG(m) ( (unsigned)((m) - LEFT_DRAG) <= \
                               (unsigned)(RIGHT_DRAG - LEFT_DRAG))
#define IS_MOUSE_RELEASE(m) ( (unsigned)((m) - LEFT_RELEASE) <= \
                               (unsigned)(RIGHT_RELEASE - LEFT_RELEASE))

/* Bit flags indicating mouse button priorities */
#define BUTTON_BEATS(x,y) ( 1 << (((x)-LEFT_BUTTON)*3+(y)-LEFT_BUTTON) )

#define IGNOREARG(x) ( (x) = (x) )

typedef struct frontend frontend;
typedef struct config_item config_item;
typedef struct midend_data midend_data;
typedef struct random_state random_state;
typedef struct game_params game_params;
typedef struct game_state game_state;
typedef struct game_aux_info game_aux_info;
typedef struct game_ui game_ui;
typedef struct game_drawstate game_drawstate;
typedef struct game game;
typedef struct blitter blitter;

#define ALIGN_VNORMAL 0x000
#define ALIGN_VCENTRE 0x100

#define ALIGN_HLEFT   0x000
#define ALIGN_HCENTRE 0x001
#define ALIGN_HRIGHT  0x002

#define FONT_FIXED    0
#define FONT_VARIABLE 1

/*
 * Structure used to pass configuration data between frontend and
 * game
 */
enum { C_STRING, C_CHOICES, C_BOOLEAN, C_END };
struct config_item {
    /*
     * `name' is never dynamically allocated.
     */
    char *name;
    /*
     * `type' contains one of the above values.
     */
    int type;
    /*
     * For C_STRING, `sval' is always dynamically allocated and
     * non-NULL. For C_BOOLEAN and C_END, `sval' is always NULL.
     * For C_CHOICES, `sval' is non-NULL, _not_ dynamically
     * allocated, and contains a set of option strings separated by
     * a delimiter. The delimeter is also the first character in
     * the string, so for example ":Foo:Bar:Baz" gives three
     * options `Foo', `Bar' and `Baz'.
     */
    char *sval;
    /*
     * For C_BOOLEAN, this is TRUE or FALSE. For C_CHOICES, it
     * indicates the chosen index from the `sval' list. In the
     * above example, 0==Foo, 1==Bar and 2==Baz.
     */
    int ival;
};

/*
 * Platform routines
 */

/* We can't use #ifdef DEBUG, because Cygwin defines it by default. */
#ifdef DEBUGGING
#define debug(x) (debug_printf x)
void debug_printf(char *fmt, ...);
#else
#define debug(x)
#endif

void fatal(char *fmt, ...);
void frontend_default_colour(frontend *fe, float *output);
void draw_text(frontend *fe, int x, int y, int fonttype, int fontsize,
               int align, int colour, char *text);
void draw_rect(frontend *fe, int x, int y, int w, int h, int colour);
void draw_line(frontend *fe, int x1, int y1, int x2, int y2, int colour);
void draw_polygon(frontend *fe, int *coords, int npoints,
                  int fill, int colour);
void draw_circle(frontend *fe, int cx, int cy, int radius,
                 int fill, int colour);
void clip(frontend *fe, int x, int y, int w, int h);
void unclip(frontend *fe);
void start_draw(frontend *fe);
void draw_update(frontend *fe, int x, int y, int w, int h);
void end_draw(frontend *fe);
void deactivate_timer(frontend *fe);
void activate_timer(frontend *fe);
void status_bar(frontend *fe, char *text);
void get_random_seed(void **randseed, int *randseedsize);

blitter *blitter_new(int w, int h);
void blitter_free(blitter *bl);
/* save puts the portion of the current display with top-left corner
 * (x,y) to the blitter. load puts it back again to the specified
 * coords, or else wherever it was saved from
 * (if x = y = BLITTER_FROMSAVED). */
void blitter_save(frontend *fe, blitter *bl, int x, int y);
#define BLITTER_FROMSAVED (-1)
void blitter_load(frontend *fe, blitter *bl, int x, int y);

/*
 * midend.c
 */
midend_data *midend_new(frontend *fe, const game *ourgame);
void midend_free(midend_data *me);
void midend_set_params(midend_data *me, game_params *params);
void midend_size(midend_data *me, int *x, int *y, int expand);
void midend_new_game(midend_data *me);
void midend_restart_game(midend_data *me);
void midend_stop_anim(midend_data *me);
int midend_process_key(midend_data *me, int x, int y, int button);
void midend_force_redraw(midend_data *me);
void midend_redraw(midend_data *me);
float *midend_colours(midend_data *me, int *ncolours);
void midend_timer(midend_data *me, float tplus);
int midend_num_presets(midend_data *me);
void midend_fetch_preset(midend_data *me, int n,
                         char **name, game_params **params);
int midend_wants_statusbar(midend_data *me);
enum { CFG_SETTINGS, CFG_SEED, CFG_DESC };
config_item *midend_get_config(midend_data *me, int which, char **wintitle);
char *midend_set_config(midend_data *me, int which, config_item *cfg);
char *midend_game_id(midend_data *me, char *id);
char *midend_text_format(midend_data *me);
char *midend_solve(midend_data *me);
void midend_supersede_game_desc(midend_data *me, char *desc, char *privdesc);
char *midend_rewrite_statusbar(midend_data *me, char *text);

/*
 * malloc.c
 */
void *smalloc(size_t size);
void *srealloc(void *p, size_t size);
void sfree(void *p);
char *dupstr(const char *s);
#define snew(type) \
    ( (type *) smalloc (sizeof (type)) )
#define snewn(number, type) \
    ( (type *) smalloc ((number) * sizeof (type)) )
#define sresize(array, number, type) \
    ( (type *) srealloc ((array), (number) * sizeof (type)) )

/*
 * misc.c
 */
void free_cfg(config_item *cfg);
void obfuscate_bitmap(unsigned char *bmp, int bits, int decode);

/* allocates output each time. len is always in bytes of binary data.
 * May assert (or just go wrong) if lengths are unchecked. */
char *bin2hex(const unsigned char *in, int inlen);
unsigned char *hex2bin(const char *in, int outlen);


/*
 * version.c
 */
extern char ver[];

/*
 * random.c
 */
random_state *random_init(char *seed, int len);
unsigned long random_bits(random_state *state, int bits);
unsigned long random_upto(random_state *state, unsigned long limit);
void random_free(random_state *state);
char *random_state_encode(random_state *state);
random_state *random_state_decode(char *input);
/* random.c also exports SHA, which occasionally comes in useful. */
typedef unsigned long uint32;
typedef struct {
    uint32 h[5];
    unsigned char block[64];
    int blkused;
    uint32 lenhi, lenlo;
} SHA_State;
void SHA_Init(SHA_State *s);
void SHA_Bytes(SHA_State *s, void *p, int len);
void SHA_Final(SHA_State *s, unsigned char *output);
void SHA_Simple(void *p, int len, unsigned char *output);

/*
 * Data structure containing the function calls and data specific
 * to a particular game. This is enclosed in a data structure so
 * that a particular platform can choose, if it wishes, to compile
 * all the games into a single combined executable rather than
 * having lots of little ones.
 */
struct game {
    const char *name;
    const char *winhelp_topic;
    game_params *(*default_params)(void);
    int (*fetch_preset)(int i, char **name, game_params **params);
    void (*decode_params)(game_params *, char const *string);
    char *(*encode_params)(game_params *, int full);
    void (*free_params)(game_params *params);
    game_params *(*dup_params)(game_params *params);
    int can_configure;
    config_item *(*configure)(game_params *params);
    game_params *(*custom_params)(config_item *cfg);
    char *(*validate_params)(game_params *params);
    char *(*new_desc)(game_params *params, random_state *rs,
		      game_aux_info **aux, int interactive);
    void (*free_aux_info)(game_aux_info *aux);
    char *(*validate_desc)(game_params *params, char *desc);
    game_state *(*new_game)(midend_data *me, game_params *params, char *desc);
    game_state *(*dup_game)(game_state *state);
    void (*free_game)(game_state *state);
    int can_solve;
    char *(*solve)(game_state *orig, game_state *curr,
		   game_aux_info *aux, char **error);
    int can_format_as_text;
    char *(*text_format)(game_state *state);
    game_ui *(*new_ui)(game_state *state);
    void (*free_ui)(game_ui *ui);
    void (*changed_state)(game_ui *ui, game_state *oldstate,
                          game_state *newstate);
    char *(*interpret_move)(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button);
    game_state *(*execute_move)(game_state *state, char *move);
    void (*size)(game_params *params, game_drawstate *ds, int *x, int *y,
                 int expand);
    float *(*colours)(frontend *fe, game_state *state, int *ncolours);
    game_drawstate *(*new_drawstate)(game_state *state);
    void (*free_drawstate)(game_drawstate *ds);
    void (*redraw)(frontend *fe, game_drawstate *ds, game_state *oldstate,
		   game_state *newstate, int dir, game_ui *ui, float anim_time,
		   float flash_time);
    float (*anim_length)(game_state *oldstate, game_state *newstate, int dir,
			 game_ui *ui);
    float (*flash_length)(game_state *oldstate, game_state *newstate, int dir,
			  game_ui *ui);
    int (*wants_statusbar)(void);
    int is_timed;
    int (*timing_state)(game_state *state);
    int mouse_priorities;
};

/*
 * For one-game-at-a-time platforms, there's a single structure
 * like the above, under a fixed name. For all-at-once platforms,
 * there's a list of all available puzzles in array form.
 */
#ifdef COMBINED
extern const game *gamelist[];
extern const int gamecount;
#else
extern const game thegame;
#endif

#endif /* PUZZLES_PUZZLES_H */
