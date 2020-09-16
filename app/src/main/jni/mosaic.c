/*
 * mosaic.c: A puzzle based on a square grid, with some of the tiles
 * having clues as to how many black squares are around them.
 * the purpose of the game is to find what should be on all tiles (black or unmarked)
 * 
 * The game is also known as: ArtMosaico, Count and Darken, Cuenta Y Sombrea, Fill-a-Pix,
 * Fill-In, Komsu Karala, Magipic, Majipiku, Mosaico, Mosaik, Mozaiek, Nampre Puzzle, Nurie-Puzzle, Oekaki-Pix, Voisimage.
 * 
 * Implementation is loosely based on https://github.com/mordechaim/Mosaic, UI interaction is based on
 * the range puzzle in the collection.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#define DEFAULT_SIZE 10
#define DEFAULT_LEVEL 3
#define SOLVE_MAX_ITERATIONS 250
#define MAX_TILES 10000
#define MAX_TILES_ERROR "Maximum size is 10000 tiles"
#define DEFAULT_TILE_SIZE 32
#define DEBUG_IMAGE 1
#undef DEBUG_IMAGE

/* To enable debug prints define DEBUG_PRINTS */

/* Getting the coordinates and returning NULL when out of scope 
 * The absurd amount of parentesis is needed to avoid order of operations issues */
#define get_cords(params, array, x, y) (((x) >= 0 && (y) >= 0) && ((x)< params->width && (y)<params->height))? \
     array + ((y)*params->width)+x : NULL;

enum {
    COL_BACKGROUND = 0,
    COL_UNMARKED,
    COL_GRID,
    COL_MARKED,
    COL_BLANK,
    COL_TEXT_SOLVED,
    COL_ERROR,
    COL_LOWLIGHT,
    COL_TEXT_DARK = COL_MARKED,
    COL_TEXT_LIGHT = COL_BLANK,
    COL_HIGHLIGHT = COL_ERROR, /* mkhighlight needs it, I don't */
    COL_CURSOR = COL_LOWLIGHT,
    NCOLOURS
};

enum cell_state {
    STATE_UNMARKED = 0,
    STATE_MARKED = 1,
    STATE_BLANK = 2,
    STATE_SOLVED = 4,
    STATE_ERROR = 8,
    STATE_UNMARKED_ERROR = STATE_ERROR | STATE_UNMARKED,
    STATE_MARKED_ERROR = STATE_ERROR | STATE_MARKED,
    STATE_BLANK_ERROR = STATE_ERROR | STATE_BLANK,
    STATE_BLANK_SOLVED = STATE_SOLVED | STATE_BLANK,
    STATE_MARKED_SOLVED = STATE_MARKED | STATE_SOLVED,
    STATE_OK_NUM = STATE_BLANK | STATE_MARKED
};

struct game_params {
    int width;
    int height;
    int level;
    bool advanced;
};

typedef struct board_state board_state;

struct game_state {
    bool cheating;
    int not_completed_clues;
    int width;
    int height;
    char *cells_contents;
    board_state *board;
};

struct board_state {
    unsigned int references;
    struct board_cell *actual_board;
};

struct board_cell
{
    char clue;
    bool shown;
};

struct solution_cell {
    char cell;
    bool solved;
    bool needed;
};

struct desc_cell
{
    char clue;
    bool shown;
    bool value;
    bool full;
    bool empty;
};

struct game_ui
{
    bool solved;
    bool in_progress;
    int last_x, last_y, last_state;
};


static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->width = DEFAULT_SIZE;
    ret->height = DEFAULT_SIZE;
    ret->advanced = false;
    ret->level = DEFAULT_LEVEL;

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    const int sizes[6] = {3, 3, 10, 15, 25, 50};
    if (i < 0 || i > 5) {
        return false;
    }
    const int levels[6] = {3, 1, 3, 2, 3, 4};
    game_params *res = snew(game_params);
    res->height = sizes[i];
    res->width = sizes[i];
    res->level = levels[i];
    res->advanced = false;
    *params=res;
    char *value = snewn(25, char);
    sprintf(value, "Size: %dx%d, level: %d", sizes[i], sizes[i], levels[i]);
    *name = value;
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

static void decode_params(game_params *params, char const *string)
{
    char *curr = strchr(string, 'x');
    char temp[15] = "";
    char *prev = NULL;
    int loc = 0;
    if (!curr) {
        return;
    }
    strncpy(temp, string, curr-string);
    params->width = atol(temp);
    prev = curr;
    curr = strchr(string, 'l');
    if (curr) {
        strncpy(temp, prev + 1, curr - prev);
        params->height = atol(temp);
    } else {
        curr = strchr(string, 'a');
        strncpy(temp, prev + 1, curr - prev);
        params->height = atol(temp);
    }
    curr++;
    while (*curr != 'a' && *curr != '\0')
    {
        temp[loc] = *curr;
        loc++;
        curr++;
    }
    temp[loc] = '\0';
    params->level = atol(temp);
}

static char *encode_params(const game_params *params, bool full)
{
    char encoded[20] = "";
    if (full) {
        sprintf(encoded, "%dx%dl%da%d", params->width, params->height, params->level, params->advanced);
    } else {
        sprintf(encoded, "%dx%da%d", params->width, params->height, params->advanced);
    }
    return dupstr(encoded);
}

static config_item *game_configure(const game_params *params)
{
    config_item *config = snewn(5, config_item);
    char *value = snewn(12, char);
    config[0].type=C_STRING;
    config[0].name="Height";
    sprintf(value,"%d", params->height);
    config[0].u.string.sval=value;
    value = snewn(12, char);
    config[1].type=C_STRING;
    config[1].name="Width";
    sprintf(value,"%d", params->width);
    config[1].u.string.sval=value;
    value = snewn(12, char);
    config[2].type=C_STRING;
    config[2].name="Level";
    sprintf(value,"%d", params->level);
    config[2].u.string.sval=value;
    config[3].name="Advanced (unsupported)";
    config[3].type=C_BOOLEAN;
    config[3].u.boolean.bval = params->advanced;
    config[4].type=C_END;
    return config;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *res = snew(game_params);
    res->height=atol(cfg[0].u.string.sval);
    res->width=atol(cfg[1].u.string.sval);
    res->level=atol(cfg[2].u.string.sval);
    res->advanced=cfg[3].u.boolean.bval;
    return res;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->advanced && full) {
        return "Cannot generate advanced puzzle";
    }
    if (params->height < 3 || params->width < 3) {
        return "Minimal size is 3x3";
    }
    if (params->height * params->width > MAX_TILES) {
        return MAX_TILES_ERROR;
    }
    if (params->level < 0) {
        return "Level must be a positive number";
    }
    return NULL;
}

static bool get_pixel(const game_params *params, const bool *image, const int x, const int y) {
    const bool *pixel;
    pixel = get_cords(params, image, x, y);
    if (pixel) {
        return *pixel;
    }
    return 0;
}

static void populate_cell(const game_params *params, const bool *image, const int x, const int y, bool edge, struct desc_cell *desc) {
    int clue = 0;
    bool xEdge = false;
    bool yEdge = false;
    if (edge) {
        if (x > 0) {
            clue += get_pixel(params, image, x-1, y); 
            if (y > 0) {
                clue += get_pixel(params, image, x-1, y-1);
            }
            if (y < params->height-1) {
                clue += get_pixel(params, image, x-1, y+1);
            }
        } else {
            xEdge = true;
        }

        if (y > 0) {
            clue += get_pixel(params, image, x, y-1);
        } else {
            yEdge = true;
        }
        if (x < params->width-1) {
            clue += get_pixel(params, image, x+1, y);
            if (y>0) {
                clue += get_pixel(params, image, x+1, y-1);
            }
            if (y < params->height-1) {
                clue += get_pixel(params, image, x+1, y+1);
            }
        } else {
            xEdge = true;
        }
        if (y < params->height-1) {
            clue += get_pixel(params, image, x, y+1);
        } else {
            yEdge = true;
        }
    } else {
        clue += get_pixel(params, image, x-1, y-1);
        clue += get_pixel(params, image, x-1, y);
        clue += get_pixel(params, image, x-1, y+1);
        clue += get_pixel(params, image, x, y-1);
        clue += get_pixel(params, image, x, y+1);
        clue += get_pixel(params, image, x+1, y-1);
        clue += get_pixel(params, image, x+1, y);
        clue += get_pixel(params, image, x+1, y+1);
    }
    
    desc->value = get_pixel(params, image, x, y);
    clue += desc->value;
    if (clue == 0) {
        desc->empty = true;
        desc->full = false;
    } else {
        desc->empty = false;
        /* setting the default */
        desc->full = false;
        if (clue == 9) {
            desc->full = true;
        } else if (edge &&
            ((xEdge && yEdge && clue == 4) ||
            ((xEdge || yEdge) && clue == 6))) {
            
            desc->full = true;
        }
    }
    desc->shown = true;
    desc->clue = clue;
}

static void count_around(const game_params *params, struct solution_cell *sol, int x, int y, int *marked, int *blank, int *total) {
    int i, j;
    struct solution_cell *curr = NULL;
    (*total)=0;
    (*blank)=0;
    (*marked)=0;
 
    for (i=-1; i < 2; i++) {
        for (j=-1; j < 2; j++) {
            curr=get_cords(params, sol, x+i, y+j);
            if (curr) {
                (*total)++;
                if ((curr->cell & STATE_BLANK) != 0) {
                    (*blank)++;
                } else if ((curr->cell & STATE_MARKED) != 0) {
                    (*marked)++;
                }
            }
        }
    }
}

static void count_around_state(const game_state *state, int x, int y, int *marked, int *blank, int *total) {
    int i, j;
    char *curr = NULL;
    (*total)=0;
    (*blank)=0;
    (*marked)=0;
 
    for (i=-1; i < 2; i++) {
        for (j=-1; j < 2; j++) {
            curr=get_cords(state, state->cells_contents, x+i, y+j);
            if (curr) {
                (*total)++;
                if ((*curr & STATE_BLANK) != 0) {
                    (*blank)++;
                } else if ((*curr & STATE_MARKED)!=0) {
                    (*marked)++;
                }
            }
        }
    }
}

static void count_clues_around(const game_params *params,  struct desc_cell *desc, int x, int y, int *clues, int *total) {
    int i, j;
    struct desc_cell *curr = NULL;
    (*total)=0;
    (*clues)=0;
 
    for (i=-1; i < 2; i++) {
        for (j=-1; j < 2; j++) {
            curr=get_cords(params, desc, x+i, y+j);
            if (curr) {
                (*total)++;
                if (curr->shown) {
                    (*clues)++;
                }
            }
        }
    }
}

static void mark_around(const game_params *params, struct solution_cell *sol, int x, int y, int mark) {
    int i, j, marked = 0;
    struct solution_cell *curr;

    for (i=-1; i < 2; i++) {
        for (j=-1; j < 2; j++) {
            curr=get_cords(params, sol, x+i, y+j);
            if (curr) {
                if (curr->cell == STATE_UNMARKED) {
                    curr->cell = mark;
                    marked++;
                }
            }
        }
    }
}

static char solve_cell(const game_params *params, struct desc_cell *desc, struct board_cell *board, struct solution_cell *sol, int x, int y) {
    struct desc_cell curr;
    
    if (desc) {
        curr.shown = desc[(y*params->width)+x].shown;
        curr.clue = desc[(y*params->width)+x].clue;
        curr.full = desc[(y*params->width)+x].full;
        curr.empty = desc[(y*params->width)+x].empty;
    } else {
        curr.shown = board[(y*params->width)+x].shown;
        curr.clue = board[(y*params->width)+x].clue;
        curr.full = false;
        curr.empty = false;
    }
    int marked = 0, total = 0, blank = 0;

    if (sol[(y*params->width)+x].solved) {
        return 0;
    }
    count_around(params, sol, x, y, &marked, &blank, &total);
    if (curr.full && curr.shown) {
        sol[(y*params->width)+x].solved = true;
        if (marked+blank < total) {
            sol[(y*params->width)+x].needed = true;
        }
        mark_around(params, sol, x, y, STATE_MARKED);
        return 1;
    }
    if (curr.empty && curr.shown)
    {
        sol[(y*params->width)+x].solved = true;
        if (marked+blank < total) {
            sol[(y*params->width)+x].needed = true;
        }
        mark_around(params, sol, x, y, STATE_BLANK);
        return 1;
    }
    if (curr.shown) {
        if (!sol[(y*params->width)+x].solved) {
            if (marked == curr.clue) {
                sol[(y*params->width)+x].solved = true;
                if (total != marked + blank) {
                    sol[(y*params->width)+x].needed = true;
                }
                mark_around(params, sol, x, y, STATE_BLANK);
            } else if (curr.clue == (total - blank)) {
                sol[(y*params->width)+x].solved = true;
                if (total != marked + blank) {
                    sol[(y*params->width)+x].needed = true;
                }
                mark_around(params, sol, x, y, STATE_MARKED);
            } else if (total == marked + blank) {
                return -1;
            } else {
                return 0;
            }
            return 1;
        }
        return 0;
    } else if (total == marked + blank) {
        sol[(y*params->width)+x].solved = true;
        return 1;
    } else {
        return 0;
    }
}

static bool solve_check(const game_params *params, struct desc_cell *desc, random_state *rs, struct solution_cell **sol_return) {
    int x,y;
    struct solution_cell *sol = snewn(params->height*params->width, struct solution_cell);
    int solved = 0, iter = 0, curr = 0;

    memset(sol, 0, params->height*params->width * sizeof(struct solution_cell));
    solved = 0;
    while (solved < params->height*params->width && iter < SOLVE_MAX_ITERATIONS) {
        for (y=0; y< params->height; y++) {
            for (x=0; x < params->width; x++) {
                if (rs) {
                    curr = solve_cell(params, desc, NULL, sol, random_upto(rs, params->width), random_upto(rs, params->height));
                } else {
                    curr = solve_cell(params, desc, NULL, sol, x, y);    
                }
                if (curr < 0) {
                    iter = SOLVE_MAX_ITERATIONS;
#ifdef DEBUG_PRINTS
                    printf("error in cell x=%d, y=%d\n", x, y);
#endif
                    break;
                }
                solved += curr;
            }
        }
        iter++;
    }
    if (sol_return) {
        *sol_return = sol;
    } else {
        sfree(sol);
    }
    return solved == params->height*params->width;
}

static bool solve_game_actual(const game_params *params, struct board_cell *desc, random_state *rs, struct solution_cell **sol_return) {
    int x,y;
    struct solution_cell *sol = snewn(params->height*params->width, struct solution_cell);
    int solved = 0, iter = 0, curr = 0;

    memset(sol, 0, params->height*params->width * sizeof(struct solution_cell));
    solved = 0;
    while (solved < params->height*params->width && iter < SOLVE_MAX_ITERATIONS) {
        for (y=0; y< params->height; y++) {
            for (x=0; x < params->width; x++) {
                if (rs) {
                    curr = solve_cell(params, NULL, desc, sol, random_upto(rs, params->width), random_upto(rs, params->height));
                } else {
                    curr = solve_cell(params, NULL, desc, sol, x, y);    
                }
                if (curr < 0) {
                    iter = SOLVE_MAX_ITERATIONS;
#ifdef DEBUG_PRINTS
                    printf("error in cell x=%d, y=%d\n", x, y);
#endif
                    break;
                }
                solved += curr;
            }
        }
        iter++;
    }
    if (sol_return) {
        *sol_return = sol;
    } else {
        sfree(sol);
    }
    return solved == params->height*params->width;
}


static void hide_clues(const game_params *params, struct desc_cell *desc, random_state *rs){
    int shown, total, x, y, count1 = 0, count2 = 0, count3 = 0;
#ifdef DEBUG_PRINTS
    int needed = 0;
#endif
    struct desc_cell *curr;
    struct solution_cell *sol = NULL, *sol2 = NULL, *sol3 = NULL, *curr_sol = NULL;
    
#ifdef DEBUG_PRINTS
    printf("Hiding clues\n");
#endif
    solve_check(params, desc, rs, &sol);
    if (params->level == 0) {
        solve_check(params, desc, rs, &sol2);
        solve_check(params, desc, rs, &sol3);
        for (y=0; y< params->height; y++) {
            for (x=0; x < params->width; x++) {
                curr_sol = get_cords(params, sol, x, y);
                if (curr_sol->needed) {
                    count1++;
                }
                curr_sol = get_cords(params, sol2, x, y);
                if (curr_sol->needed) {
                    count2++;
                }
                curr_sol = get_cords(params, sol3, x, y);
                if (curr_sol->needed) {
                    count3++;
                }
            }
        }
        if (count1 <= count2) {
            sfree(sol2);
            if (count1 <= count3) {
                sfree(sol3);
            } else if (count1 > count3) {
                sfree(sol);
                sol=sol3;
            }
        } else {
            sfree(sol);
            if (count2 <= count3) {
                sfree(sol3);
                sol = sol2;
            } else {
                sfree(sol3);
                sol = sol3;
            }
        }
    }
    for (y=0; y< params->height; y++) {
        for (x=0; x < params->width; x++) {
            count_clues_around(params, desc, x, y, &shown, &total);                
            curr = get_cords(params, desc, x, y);
            curr_sol = get_cords(params, sol, x, y);
#ifdef DEBUG_PRINTS
            if (curr_sol->needed) {
                needed++;
            }
#endif
            if (curr_sol->needed == false) {
                if (!params->level || random_upto(rs, params->level) <= 1) {
                    curr->shown=false;
                }
            }
        }
    }
#ifdef DEBUG_PRINTS
    printf("needed %d\n", needed);
#endif
    sfree(sol);
}

static bool start_point_check(size_t size, struct desc_cell *desc) {
    int i;
    for (i=0; i < size; i++)
    {
        if (desc[i].empty || desc[i].full){
            return true;
        }
    }
    return false;
}

static void generate_image(const game_params *params, random_state *rs, bool *image) {
    int x,y;
    for (y=0; y< params->height; y++) {
        for (x=0; x < params->width; x++) {
            image[(y*params->width)+x]=random_bits(rs, 1);
        }
    }
}

static char *new_game_desc(const game_params *params, random_state *rs,
			   char **aux, bool interactive)
{
    bool *image = snewn(params->height*params->width, bool);
    bool valid = false;
    char *desc_string = snewn((params->height*params->width)+1, char);
    char *compressed_desc = snewn((params->height*params->width)+1, char);
    char space_count;

    struct desc_cell* desc=snewn(params->height*params->width, struct desc_cell);    
    int x,y, location_in_str;

    while (!valid) {
        generate_image(params, rs, image);
#ifdef DEBUG_IMAGE
        image[0] = 1;
        image[1] = 1;
        image[2] = 0;
        image[3] = 1;
        image[4] = 1;
        image[5] = 0;
        image[6] = 0;
        image[7] = 0;
        image[8] = 0;
#endif

        for (y=0; y< params->height; y++) {
            for (x=0; x < params->width; x++) {
                populate_cell(params, image, x, y, x*y == 0 || y == params->height - 1 || x == params->width -1, &desc[(y*params->width)+x]);
            }
        }
        valid = start_point_check((params->height-1) * (params->width-1), desc);
        if (!valid) {
#ifdef DEBUG_PRINTS
            printf("Not valid, regenerating.\n");
#endif
        } else {
            valid = solve_check(params, desc, rs, NULL);
            if (!valid) {
#ifdef DEBUG_PRINTS
                printf("Couldn't solve, regenerating.");
#endif
            } else {
                hide_clues(params, desc, rs);
            }
        }
    }
    location_in_str = 0;
    for (y=0; y< params->height; y++) {
        for (x=0; x < params->width; x++) {
            if (desc[(y*params->width)+x].shown) {
#ifdef DEBUG_PRINTS
                printf("%d(%d)", desc[(y*params->width)+x].value, desc[(y*params->width)+x].clue);
#endif
                sprintf(desc_string + location_in_str, "%d", desc[(y*params->width)+x].clue);
            } else {
#ifdef DEBUG_PRINTS
                printf("%d( )", desc[(y*params->width)+x].value);
#endif
                sprintf(desc_string + location_in_str, " ");
            }
            location_in_str+=1;
        }
#ifdef DEBUG_PRINTS
        printf("\n");
#endif
    }
    location_in_str = 0;
    space_count='a'-1;
    for (y=0; y< params->height; y++) {
        for (x=0; x < params->width; x++) {
            if (desc[(y*params->width)+x].shown) {
                if (space_count >= 'a') {
                    sprintf(compressed_desc + location_in_str, "%c", space_count);
                    location_in_str++;
                    space_count = 'a'-1; 
                }
                sprintf(compressed_desc + location_in_str, "%d", desc[(y*params->width)+x].clue);
                location_in_str++;
            } else {
                if (space_count <= 'z') {
                    space_count++;
                } else {
                    sprintf(compressed_desc + location_in_str, "%c", space_count);
                    location_in_str++;
                    space_count = 'a'-1;
                }
            }
        }
    }
    if (space_count >= 'a') {
        sprintf(compressed_desc + location_in_str, "%c", space_count);
        location_in_str++;
    }
    compressed_desc[location_in_str] = '\0';
#ifdef DEBUG_PRINTS
    printf("compressed_desc: %s\n", compressed_desc);
#endif
    return compressed_desc;
}



static const char *validate_desc(const game_params *params, const char *desc)
{
    int size_dest = params->height*params->width;
    char *curr_desc = dupstr(desc);
    char *desc_base = curr_desc;
    int length;
    length = 0;

    while (*curr_desc != '\0')
    {
        if (*curr_desc >= 'a' && *curr_desc <= 'z') {
            length += *curr_desc-'a';
        }
        length++;
        curr_desc++;
    }

    sfree(desc_base);
    if (length != size_dest) {
        return "Desc size mismatch";
    }
    return NULL;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    game_state *state = snew(game_state);
    char *curr_desc = dupstr(desc);
    char *desc_base = curr_desc;
    int dest_loc;
    int spaces, total_spaces;

    state->cheating = false;
    state->not_completed_clues = 0;
    dest_loc = 0;
    state->height = params->height;
    state->width = params->width;
    state->cells_contents = snewn(params->height*params->width, char);
    memset(state->cells_contents, 0, params->height*params->width* sizeof(char));
    state->board = snew(board_state);
    state->board->references = 1;
    state->board->actual_board = snewn(params->height*params->width, struct board_cell);

    while (*curr_desc != '\0') {
        if (*curr_desc >= '0' && *curr_desc <= '9'){
            state->board->actual_board[dest_loc].shown = true;
            state->not_completed_clues++;
            state->board->actual_board[dest_loc].clue = *curr_desc - '0';
        } else {
            if (*curr_desc != ' ') {
                total_spaces = *curr_desc - 'a' + 1;
            } else {
                total_spaces = 1;
            }
            spaces = 0;
            while (spaces < total_spaces)
            {
                state->board->actual_board[dest_loc].shown = false;
                state->board->actual_board[dest_loc].clue = -1;
                spaces++;
                if (spaces < total_spaces) {
                    dest_loc++;
                }
            }
        }
        curr_desc++;
        dest_loc++;
    }

    sfree(desc_base);
    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->cheating = state->cheating;
    ret->width = state->width;
    ret->height = state->height;
    ret->cells_contents = snewn(state->height*state->width, char);
    memcpy(ret->cells_contents, state->cells_contents, state->height*state->width * sizeof(char));
    ret->board = state->board;
    ret->board->references++;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->cells_contents);
    state->cells_contents = NULL;
    if (state->board->references <= 1) {
        sfree(state->board);
        state->board = NULL;
    } else {
        state->board->references--;
    }
    sfree(state);
}

static char *solve_game(const game_state *state, const game_state *currstate,
                        const char *aux, const char **error)
{
    struct solution_cell *sol = NULL;
    game_params param;
    bool solved;
    char *ret=NULL;
    unsigned int curr_ret;
    int i, bits, ret_loc = 1;
    int size = state->width*state->height;

    param.width = state->width;
    param.height = state->height;
    param.advanced = false;
    solved = solve_game_actual(&param, state->board->actual_board, NULL, &sol);
    if (!solved) {
        *error = dupstr("Could not solve this board");
        sfree(sol);
        return NULL;
    }

    ret=snewn((size/4)+3, char);

    ret[0] = 's';
    i=0;
    while (i < size)
    {
        curr_ret = 0;
        bits = 0;
        while (bits < 8 && i < size) {
            curr_ret <<= 1;
            curr_ret |= sol[i].cell == STATE_MARKED;
            i++;
            bits++;
        }
        curr_ret <<= 8-bits; 
        sprintf(ret + ret_loc, "%02x", curr_ret);
        ret_loc += 2;
    }
    
    sfree(sol);
    return ret;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    char *desc_string = snewn((state->height*state->width)*3+1, char);
    int location_in_str = 0, x, y;
    for (y=0; y < state->height; y++) {
        for (x=0; x < state->width; x++) {
            if (state->board->actual_board[(y*state->width)+x].shown) {
                sprintf(desc_string + location_in_str, "|%d|", state->board->actual_board[(y*state->width)+x].clue);
            } else {
                sprintf(desc_string + location_in_str, "| |");
            }
            location_in_str+=3;
        }
        sprintf(desc_string + location_in_str, "\n");
        location_in_str+=1;
    }
    return desc_string;
}

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->last_x = -1;
    ui->last_y = -1;
    ui->last_state = 0;
    ui->solved = false;
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
    ui->last_x = -1;
    ui->last_y = -1;
    ui->last_state = 0;
    ui->solved = false;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
}

struct game_drawstate {
    int tilesize;
};

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int gameX, gameY;
    char move_type;
    char move_desc[30] = "";
    char *ret = NULL;
    const char *cell_state;
    if (state->not_completed_clues == 0) {
        return NULL;
    }
    gameX = (x-(ds->tilesize/2))/ds->tilesize;
    gameY = (y-(ds->tilesize/2))/ds->tilesize;
    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        cell_state = get_cords(state, state->cells_contents, gameX, gameY);
        if (cell_state) {
            ui->last_state = *cell_state & (STATE_BLANK | STATE_MARKED);
            ui->last_state = (ui->last_state + ((button == RIGHT_BUTTON) ? 2 : 1)) % (STATE_BLANK | STATE_MARKED);
        }
        if (button == RIGHT_BUTTON) {
            /* Right button toggles twice */
            move_type = 'T';
        } else {
            move_type = 't';
        }
        if (gameX >= 0 && gameY >= 0 && gameX < state->width && gameY < state->height) {
            sprintf(move_desc, "%c%d,%d", move_type, gameX, gameY);
            ui->last_x = gameX;
            ui->last_y = gameY;
            ret = dupstr(move_desc);
        } else {
            ui->last_x = -1;
            ui->last_y = -1;
        }
    } else if (button == LEFT_DRAG || button == RIGHT_DRAG) {
        move_type = 'd';
        /* allowing only drags in straight lines */
        if (gameX >= 0 && gameY >= 0 && gameX < state->width && gameY < state->height && ui->last_x >= 0 && ui->last_y >= 0 &&
            (gameY == ui->last_y || gameX == ui->last_x)) {
            sprintf(move_desc, "%c%d,%d,%d,%d,%d", move_type, gameX, gameY, ui->last_x, ui->last_y, ui->last_state);
            ui->last_x = gameX;
            ui->last_y = gameY;
            ret = dupstr(move_desc);
        } else {
            ui->last_x = -1;
            ui->last_y = -1;
        }
    } else if (button == LEFT_RELEASE|| button == RIGHT_RELEASE) {
        move_type = 'e';
        if (gameX >= 0 && gameY >= 0 && gameX < state->width && gameY < state->height && ui->last_x >= 0 && ui->last_y >= 0 &&
        (gameY == ui->last_y || gameX == ui->last_x)) {
            sprintf(move_desc, "%c%d,%d,%d,%d,%d", move_type, gameX, gameY, ui->last_x, ui->last_y, ui->last_state);
            ret = dupstr(move_desc);
        } else {
            ui->last_x = -1;
            ui->last_y = -1;
        }
    }
    return ret;
}

static void update_board_state_around(game_state *state, int x, int y) {
    int i, j;
    struct board_cell *curr;
    char *curr_state;
    int total;
    int blank;
    int marked;
 
    for (i=-1; i < 2; i++) {
        for (j=-1; j < 2; j++) {
            curr=get_cords(state, state->board->actual_board, x+i, y+j);
            if (curr && curr->shown) {
                curr_state = get_cords(state, state->cells_contents, x+i, y+j);
                count_around_state(state, x+i, y+j, &marked, &blank, &total);
                if (curr->clue == marked && (total - marked - blank) == 0) {
                    *curr_state &= STATE_MARKED | STATE_BLANK;
                    *curr_state |= STATE_SOLVED;
                } else if (curr->clue < marked || curr->clue > (total - blank)) {
                    *curr_state &= STATE_MARKED | STATE_BLANK;
                    *curr_state |= STATE_ERROR;
                } else {
                    *curr_state &= STATE_MARKED | STATE_BLANK;
                }
            }
        }
    }
}



static game_state *execute_move(const game_state *state, const char *move)
{
    game_state *new_state = dup_game(state);
    int i = 0, x = -1, y = -1, clues_left = 0;
    int srcX = -1, srcY = -1, size = state->height * state->width;
    char *comma, *cell, sol_char;
    char coordinate[30] = "";
    int steps = 1, bits, sol_location, dirX, dirY, diff, last_state;
    unsigned int sol_value;
    struct board_cell *curr_cell;
    /* Check location */
    if (move[0] == 't' || move[0] == 'T' || move[0] == 'd' || move[0] == 'e') {
        i++;
        comma=strchr(move + i, ',');
        if (comma != NULL) {
            memset(coordinate, 0, sizeof(char)*12);
            strncpy(coordinate, move + i, comma - move - 1);
            x = atol(coordinate);
            i = comma - move;
            i++;
            memset(coordinate, 0, sizeof(char)*12);
            comma=strchr(move + i, ',');
            if (comma) {
                strncpy(coordinate, move + i, comma - move - 1);
                y = atol(coordinate);
                i+=2;
                comma=strchr(move + i, ',');
                strncpy(coordinate, move + i, comma - move - 1);
                srcX = atol(coordinate);
                i+=2;
                comma=strchr(move + i, ',');
                strncpy(coordinate, move + i, comma - move - 1);
                srcY = atol(coordinate);
                i+=2;
                strcpy(coordinate, move + i);
                last_state = atol(coordinate);
            } else {
                strcpy(coordinate, move + i);
                y = atol(coordinate);
            }
        }
    }
    if (move[0] == 't' || move[0] == 'T') {
        if (move[0] == 'T') {
            steps++;
        }
        if (x==-1 || y==-1) {
            return new_state;
        }
        cell = get_cords(new_state, new_state->cells_contents, x, y);
        if (*cell >= STATE_OK_NUM) {
            *cell &= STATE_OK_NUM;
        }
        *cell = (*cell + steps) % STATE_OK_NUM;
        update_board_state_around(new_state, x, y);
    } else if (move[0] == 's') {
        new_state->not_completed_clues = 0;
        new_state->cheating = true;
        sol_location = 0;
        bits = 0;
        i=1;
        while (i < strlen(move)) {
            sol_value=0;
            while (bits < 8) {
                sol_value <<= 4;
                sol_char = move[i];
                if (sol_char >= '0' && sol_char <= '9') {
                    sol_value |= sol_char - '0';
                } else {
                    sol_value |= (sol_char - 'a') + 10;
                }
                bits += 4;
                i++;
            }
            while (bits > 0 && sol_location < size) {
                if (sol_value & 0b10000000) {
                    new_state->cells_contents[sol_location] = STATE_MARKED_SOLVED;
                } else {
                    new_state->cells_contents[sol_location] = STATE_BLANK_SOLVED;
                }
                sol_value <<= 1;
                bits--;
                sol_location++;
            }
        }
        return new_state;
    } else if (move[0] == 'd' || move[0] == 'e') {
        if (srcX == x && srcY != y) {
            dirX = 0;
            diff = srcY - y;
            if (diff < 0) {
                dirY = -1;
                diff *= -1;
            } else {
                dirY = 1;
            }
        } else {
            diff = srcX - x;
            dirY = 0;
            if (diff < 0) {
                dirX = -1;
                diff *= -1;
            } else {
                dirX = 1;
            }
        }
        for (i =  0 ; i < diff; i++) {
            cell =  get_cords(new_state, new_state->cells_contents, x + (dirX * i), y + (dirY * i));
            if ((*cell & STATE_OK_NUM) == 0) {
                *cell = last_state;
                update_board_state_around(new_state, x + (dirX * i), y + (dirY * i));
            } 
        }
    }
    for (y=0; y < state->height; y++) {
        for (x=0; x < state->width; x++) {
            cell = get_cords(new_state, new_state->cells_contents, x, y);
            curr_cell = get_cords(new_state, new_state->board->actual_board, x, y);
            if (curr_cell->shown && ((*cell & STATE_SOLVED) == 0)) {
                clues_left++;
            }
        }
    }
    new_state->not_completed_clues=clues_left;
    return new_state;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    *x = (params->width+1) * tilesize;
    *y = (params->height+1) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

#define COLOUR(ret, i, r, g, b) \
   ((ret[3*(i)+0] = (r)), (ret[3*(i)+1] = (g)), (ret[3*(i)+2] = (b)))

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);
    COLOUR(ret, COL_GRID,  0.0F, 102/255.0F, 99/255.0F);
    COLOUR(ret, COL_ERROR, 1.0F, 0.0F, 0.0F);
    COLOUR(ret, COL_BLANK,  236/255.0F, 236/255.0F, 236/255.0F);
    COLOUR(ret, COL_MARKED,  20/255.0F, 20/255.0F, 20/255.0F);
    COLOUR(ret, COL_UNMARKED,  148/255.0F, 196/255.0F, 190/255.0F);
    COLOUR(ret, COL_TEXT_SOLVED,  100/255.0F, 100/255.0F, 100/255.0F);

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds);
}

static void draw_cell(drawing *dr, game_drawstate *ds,
                    const game_state *state,
                    int x, int y, bool flashing) {
    const int ts = ds->tilesize;
    int startX = ((x * ts) + ts/2)-1, startY = ((y * ts)+ ts/2)-1;
    int color, text_color = COL_TEXT_DARK;
    
    char *cell_p = get_cords(state, state->cells_contents, x, y);
    char cell = *cell_p;
    if (flashing) {
        cell ^= (STATE_BLANK | STATE_MARKED);
    }
    draw_rect_outline(dr, startX-1, startY-1, ts+1, ts+1, COL_GRID);

    if (cell & STATE_MARKED) {
        color = COL_MARKED;
        text_color = COL_TEXT_LIGHT;
    } else if (cell & STATE_BLANK) {
        text_color = COL_TEXT_DARK;
        color = COL_BLANK;
    } else {
        text_color = COL_TEXT_DARK;
        color = COL_UNMARKED;
    }
    if (cell & STATE_ERROR) {
        text_color = COL_ERROR;
    } else if (cell & STATE_SOLVED) {
        text_color = COL_TEXT_SOLVED;
    }

    draw_rect(dr, startX, startY, ts-1, ts-1, color);
    struct board_cell *curr = NULL;
    char clue[5];
    curr = get_cords(state, state->board->actual_board, x, y);
    if (curr && curr->shown) {
        sprintf(clue, "%d", curr->clue);
        draw_text(dr, startX + ts/2, startY + ts/2, 1, ts * 3/5,
        ALIGN_VCENTRE | ALIGN_HCENTRE, text_color, clue);
    }
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    /*
     * The initial contents of the window are not guaranteed and
     * can vary with front ends. To be on the safe side, all games
     * should start by drawing a big background-colour rectangle
     * covering the whole window.
     */
    int x, y;
    char status[20] = "";
    if (flashtime) {
        draw_rect(dr, 0, 0, (state->width+1)*ds->tilesize, (state->height+1)*ds->tilesize, COL_BLANK);    
    } else {
        draw_rect(dr, 0, 0, (state->width+1)*ds->tilesize, (state->height+1)*ds->tilesize, COL_BACKGROUND);
    }
    for (y=0;y<state->height;y++) {
        for (x=0;x<state->width;x++) {
            draw_cell(dr, ds, state, x, y, flashtime > 0);
        }
    }
    draw_update(dr, 0, 0, (state->width+1)*ds->tilesize, (state->height+1)*ds->tilesize);
    sprintf(status, "Clues left: %d", state->not_completed_clues);
    if (state->not_completed_clues == 0 && !state->cheating) {
        sprintf(status, "COMPLETED!");
#ifdef ANDROID
        if (!flashtime) {
            android_completed();
        }
#endif
    } else if (state->not_completed_clues == 0 && state->cheating) {
        sprintf(status, "Auto solved");
    }
    status_bar(dr, dupstr(status));
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    if (!oldstate->cheating && oldstate->not_completed_clues > 0 && newstate->not_completed_clues == 0) {
        return 0.7F;
    }
    return 0.0F;
}

static int game_status(const game_state *state)
{
    return 0;
}

static bool game_timing_state(const game_state *state, game_ui *ui)
{
    return state->not_completed_clues > 0;
}

static void game_print_size(const game_params *params, float *x, float *y)
{
}

static void game_print(drawing *dr, const game_state *state, int tilesize)
{
}

#ifdef ANDROID
static void android_cursor_visibility(game_ui *ui, int visible) {

}
#endif

#ifdef COMBINED
#define thegame mosaic
#endif

const struct game thegame = {
    "Mosaic", NULL, "mosaic",
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
#ifdef ANDROID
    android_cursor_visibility,
#endif
    game_changed_state,
    interpret_move,
    execute_move,
    DEFAULT_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
#ifndef NO_PRINTING
    false, false, game_print_size, game_print,     
#endif
    true,			       /* wants_statusbar */
    true, game_timing_state,
    0,				       /* flags */
};
