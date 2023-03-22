/*
 * mosaic.c: A puzzle based on a square grid, with some of the tiles
 * having clues as to how many black squares are around them.
 * the purpose of the game is to find what should be on all tiles (black or
 * unmarked)
 *
 * The game is also known as: ArtMosaico, Count and Darken, Cuenta Y Sombrea,
 * Fill-a-Pix, Fill-In, Komsu Karala, Magipic, Majipiku, Mosaico, Mosaik,
 * Mozaiek, Nampre Puzzle, Nurie-Puzzle, Oekaki-Pix, Voisimage.
 *
 * Implementation is loosely based on https://github.com/mordechaim/Mosaic, UI
 * interaction is based on the range puzzle in the collection.
 */

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

#define DEFAULT_SIZE 10
#define DEFAULT_AGGRESSIVENESS true
#define MAX_TILES 10000
#define MAX_TILES_ERROR "Maximum size is 10000 tiles"
#define DEFAULT_TILE_SIZE 32
#define DEBUG_IMAGE 1
#undef DEBUG_IMAGE
#define FLASH_TIME 0.5F
/* To enable debug prints define DEBUG_PRINTS */

/* Getting the coordinates and returning NULL when out of scope
 * The parentheses are needed to avoid order of operations issues
 */
#define get_coords(params, array, x, y)                                      \
  (((x) >= 0 && (y) >= 0) && ((x) < params->width && (y) < params->height))  \
      ? array + ((y)*params->width) + x                                      \
      : NULL

#define COORD_FROM_CELL(d) ((d * ds->tilesize) + ds->tilesize / 2) - 1

enum {
    COL_BACKGROUND = 0,
    COL_UNMARKED,
    COL_GRID,
    COL_MARKED,
    COL_BLANK,
    COL_TEXT_SOLVED,
    COL_ERROR,
    COL_CURSOR,
    NCOLOURS,
    COL_TEXT_DARK = COL_MARKED,
    COL_TEXT_LIGHT = COL_BLANK
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
    bool aggressive;
};

typedef struct board_state board_state;

typedef struct needed_list_item needed_list_item;

struct needed_list_item {
    int x, y;
    needed_list_item *next;
};

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

struct board_cell {
    signed char clue;
    bool shown;
};

struct solution_cell {
    signed char cell;
    bool solved;
    bool needed;
};

struct desc_cell {
    char clue;
    bool shown;
    bool value;
    bool full;
    bool empty;
};

struct game_ui {
    bool solved;
    bool in_progress;
    int last_x, last_y, last_state;
    int cur_x, cur_y;
    int prev_cur_x, prev_cur_y;
    bool cur_visible;
};

struct game_drawstate {
    int tilesize;
    int *state;
    int cur_x, cur_y;           /* -1, -1 for no cursor displayed. */
    int prev_cur_x, prev_cur_y;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->width = DEFAULT_SIZE;
    ret->height = DEFAULT_SIZE;
    ret->aggressive = DEFAULT_AGGRESSIVENESS;

    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params)
{
    const int sizes[6] = { 3, 5, 10, 15, 25, 50 };
    const bool aggressiveness[6] = { true, true, true, true, true, false };
    if (i < 0 || i > 5) {
        return false;
    }
    game_params *res = snew(game_params);
    res->height = sizes[i];
    res->width = sizes[i];
    res->aggressive = aggressiveness[i];
    *params = res;

    char value[80];
    sprintf(value, "Size: %dx%d", sizes[i], sizes[i]);
    *name = dupstr(value);
    return true;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;             /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    params->width = params->height = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string++;
    if (*string == 'x') {
        string++;
        params->height = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
    if (*string == 'h') {
        string++;
        params->aggressive = atoi(string);
	while (*string && isdigit((unsigned char)*string)) string++;
    }
}

static char *encode_params(const game_params *params, bool full)
{
    char encoded[128];
    int pos = 0;
    pos += sprintf(encoded + pos, "%dx%d", params->width, params->height);
    if (full) {
        if (params->aggressive != DEFAULT_AGGRESSIVENESS)
            pos += sprintf(encoded + pos, "h%d", params->aggressive);
    }
    return dupstr(encoded);
}

static config_item *game_configure(const game_params *params)
{
    config_item *config = snewn(4, config_item);
    char value[80];

    config[0].type = C_STRING;
    config[0].name = "Height";
    sprintf(value, "%d", params->height);
    config[0].u.string.sval = dupstr(value);

    config[1].type = C_STRING;
    config[1].name = "Width";
    sprintf(value, "%d", params->width);
    config[1].u.string.sval = dupstr(value);

    config[2].name = "Aggressive generation (longer)";
    config[2].type = C_BOOLEAN;
    config[2].u.boolean.bval = params->aggressive;

    config[3].type = C_END;

    return config;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *res = snew(game_params);
    res->height = atol(cfg[0].u.string.sval);
    res->width = atol(cfg[1].u.string.sval);
    res->aggressive = cfg[2].u.boolean.bval;
    return res;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->height < 3 || params->width < 3) {
        return "Minimal size is 3x3";
    }
    if (params->height > MAX_TILES / params->width) {
        return MAX_TILES_ERROR;
    }
    return NULL;
}

static bool get_pixel(const game_params *params, const bool *image,
                      const int x, const int y)
{
    const bool *pixel;
    pixel = get_coords(params, image, x, y);
    if (pixel) {
        return *pixel;
    }
    return 0;
}

static void populate_cell(const game_params *params, const bool *image,
                          const int x, const int y, bool edge,
                          struct desc_cell *desc)
{
    int clue = 0;
    bool xEdge = false;
    bool yEdge = false;
    if (edge) {
        if (x > 0) {
            clue += get_pixel(params, image, x - 1, y);
            if (y > 0) {
                clue += get_pixel(params, image, x - 1, y - 1);
            }
            if (y < params->height - 1) {
                clue += get_pixel(params, image, x - 1, y + 1);
            }
        } else {
            xEdge = true;
        }

        if (y > 0) {
            clue += get_pixel(params, image, x, y - 1);
        } else {
            yEdge = true;
        }
        if (x < params->width - 1) {
            clue += get_pixel(params, image, x + 1, y);
            if (y > 0) {
                clue += get_pixel(params, image, x + 1, y - 1);
            }
            if (y < params->height - 1) {
                clue += get_pixel(params, image, x + 1, y + 1);
            }
        } else {
            xEdge = true;
        }
        if (y < params->height - 1) {
            clue += get_pixel(params, image, x, y + 1);
        } else {
            yEdge = true;
        }
    } else {
        clue += get_pixel(params, image, x - 1, y - 1);
        clue += get_pixel(params, image, x - 1, y);
        clue += get_pixel(params, image, x - 1, y + 1);
        clue += get_pixel(params, image, x, y - 1);
        clue += get_pixel(params, image, x, y + 1);
        clue += get_pixel(params, image, x + 1, y - 1);
        clue += get_pixel(params, image, x + 1, y);
        clue += get_pixel(params, image, x + 1, y + 1);
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
        } else if (edge && ((xEdge && yEdge && clue == 4) ||
                            ((xEdge || yEdge) && clue == 6))) {

            desc->full = true;
        }
    }
    desc->shown = true;
    desc->clue = clue;
}

static void count_around(const game_params *params,
                         struct solution_cell *sol, int x, int y,
                         int *marked, int *blank, int *total)
{
    int i, j;
    struct solution_cell *curr = NULL;
    (*total) = 0;
    (*blank) = 0;
    (*marked) = 0;

    for (i = -1; i < 2; i++) {
        for (j = -1; j < 2; j++) {
            curr = get_coords(params, sol, x + i, y + j);
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

static void count_around_state(const game_state *state, int x, int y,
                               int *marked, int *blank, int *total)
{
    int i, j;
    char *curr = NULL;
    (*total) = 0;
    (*blank) = 0;
    (*marked) = 0;

    for (i = -1; i < 2; i++) {
        for (j = -1; j < 2; j++) {
            curr = get_coords(state, state->cells_contents, x + i, y + j);
            if (curr) {
                (*total)++;
                if ((*curr & STATE_BLANK) != 0) {
                    (*blank)++;
                } else if ((*curr & STATE_MARKED) != 0) {
                    (*marked)++;
                }
            }
        }
    }
}

static void count_clues_around(const game_params *params,
                               struct desc_cell *desc, int x, int y,
                               int *clues, int *total)
{
    int i, j;
    struct desc_cell *curr = NULL;
    (*total) = 0;
    (*clues) = 0;

    for (i = -1; i < 2; i++) {
        for (j = -1; j < 2; j++) {
            curr = get_coords(params, desc, x + i, y + j);
            if (curr) {
                (*total)++;
                if (curr->shown) {
                    (*clues)++;
                }
            }
        }
    }
}

static void mark_around(const game_params *params,
                        struct solution_cell *sol, int x, int y, int mark)
{
    int i, j, marked = 0;
    struct solution_cell *curr;

    for (i = -1; i < 2; i++) {
        for (j = -1; j < 2; j++) {
            curr = get_coords(params, sol, x + i, y + j);
            if (curr) {
                if (curr->cell == STATE_UNMARKED) {
                    curr->cell = mark;
                    marked++;
                }
            }
        }
    }
}

static char solve_cell(const game_params *params, struct desc_cell *desc,
                       struct board_cell *board, struct solution_cell *sol,
                       int x, int y)
{
    struct desc_cell curr;

    if (desc) {
        curr.shown = desc[(y * params->width) + x].shown;
        curr.clue = desc[(y * params->width) + x].clue;
        curr.full = desc[(y * params->width) + x].full;
        curr.empty = desc[(y * params->width) + x].empty;
    } else {
        curr.shown = board[(y * params->width) + x].shown;
        curr.clue = board[(y * params->width) + x].clue;
        curr.full = false;
        curr.empty = false;
    }
    int marked = 0, total = 0, blank = 0;

    if (sol[(y * params->width) + x].solved) {
        return 0;
    }
    count_around(params, sol, x, y, &marked, &blank, &total);
    if (curr.full && curr.shown) {
        sol[(y * params->width) + x].solved = true;
        if (marked + blank < total) {
            sol[(y * params->width) + x].needed = true;
        }
        mark_around(params, sol, x, y, STATE_MARKED);
        return 1;
    }
    if (curr.empty && curr.shown) {
        sol[(y * params->width) + x].solved = true;
        if (marked + blank < total) {
            sol[(y * params->width) + x].needed = true;
        }
        mark_around(params, sol, x, y, STATE_BLANK);
        return 1;
    }
    if (curr.shown) {
        if (!sol[(y * params->width) + x].solved) {
            if (marked == curr.clue) {
                sol[(y * params->width) + x].solved = true;
                if (total != marked + blank) {
                    sol[(y * params->width) + x].needed = true;
                }
                mark_around(params, sol, x, y, STATE_BLANK);
            } else if (curr.clue == (total - blank)) {
                sol[(y * params->width) + x].solved = true;
                if (total != marked + blank) {
                    sol[(y * params->width) + x].needed = true;
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
        sol[(y * params->width) + x].solved = true;
        return 1;
    } else {
        return 0;
    }
}

static bool solve_check(const game_params *params, struct desc_cell *desc,
                        random_state *rs, struct solution_cell **sol_return)
{
    int x, y, i;
    int board_size = params->height * params->width;
    struct solution_cell *sol = snewn(board_size, struct solution_cell),
        *curr_sol;
    bool made_progress = true, error = false;
    int solved = 0, curr = 0, shown = 0;
    needed_list_item *head = NULL, *curr_needed, **needed_array;
    struct desc_cell *curr_desc;

    memset(sol, 0, board_size * sizeof(*sol));
    for (y = 0; y < params->height; y++) {
        for (x = 0; x < params->width; x++) {
            curr_desc = get_coords(params, desc, x, y);
            if (curr_desc->shown) {
                curr_needed = snew(needed_list_item);
                curr_needed->next = head;
                head = curr_needed;
                curr_needed->x = x;
                curr_needed->y = y;
                shown++;
            }
        }
    }
    needed_array = snewn(shown, needed_list_item *);
    curr_needed = head;
    i = 0;
    while (curr_needed) {
        needed_array[i] = curr_needed;
        curr_needed = curr_needed->next;
        i++;
    }
    if (rs) {
        shuffle(needed_array, shown, sizeof(*needed_array), rs);
    }
    solved = 0;
    while (solved < shown && made_progress && !error) {
        made_progress = false;
        for (i = 0; i < shown; i++) {
            curr = solve_cell(params, desc, NULL, sol, needed_array[i]->x,
                              needed_array[i]->y);
            if (curr < 0) {
                error = true;
#ifdef DEBUG_PRINTS
                printf("error in cell x=%d, y=%d\n", needed_array[i]->x,
                       needed_array[i]->y);
#endif
                break;
            }
            if (curr > 0) {
                solved++;
                made_progress = true;
            }
        }
    }
    while (head) {
        curr_needed = head;
        head = curr_needed->next;
        sfree(curr_needed);
    }
    sfree(needed_array);
    solved = 0;
    /* verifying all the board is solved */
    if (made_progress) {
        for (y = 0; y < params->height; y++) {
            for (x = 0; x < params->width; x++) {
                curr_sol = get_coords(params, sol, x, y);
                if ((curr_sol->cell & (STATE_MARKED | STATE_BLANK)) > 0) {
                    solved++;
                }
            }
        }
    }
    if (sol_return) {
        *sol_return = sol;
    } else {
        sfree(sol);
    }
    return solved == board_size;
}

static bool solve_game_actual(const game_params *params,
                              struct board_cell *desc,
                              struct solution_cell **sol_return)
{
    int x, y;
    int board_size = params->height * params->width;
    struct solution_cell *sol = snewn(board_size, struct solution_cell);
    bool made_progress = true, error = false;
    int solved = 0, iter = 0, curr = 0;

    memset(sol, 0, params->height * params->width * sizeof(*sol));
    solved = 0;
    while (solved < params->height * params->width && made_progress
           && !error) {
        for (y = 0; y < params->height; y++) {
            for (x = 0; x < params->width; x++) {
                curr = solve_cell(params, NULL, desc, sol, x, y);
                if (curr < 0) {
                    error = true;
#ifdef DEBUG_PRINTS
                    printf("error in cell x=%d, y=%d\n", x, y);
#endif
                    break;
                }
                if (curr > 0) {
                    made_progress = true;
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
    return solved == params->height * params->width;
}

static void hide_clues(const game_params *params, struct desc_cell *desc,
                       random_state *rs)
{
    int shown, total, x, y, i;
    int needed = 0;
    struct desc_cell *curr;
    struct solution_cell *sol = NULL, *curr_sol = NULL;
    needed_list_item *head = NULL, *curr_needed, **needed_array;

#ifdef DEBUG_PRINTS
    printf("Hiding clues\n");
#endif
    solve_check(params, desc, rs, &sol);
    for (y = 0; y < params->height; y++) {
        for (x = 0; x < params->width; x++) {
            count_clues_around(params, desc, x, y, &shown, &total);
            curr = get_coords(params, desc, x, y);
            curr_sol = get_coords(params, sol, x, y);
            if (curr_sol->needed && params->aggressive) {
                curr_needed = snew(needed_list_item);
                curr_needed->x = x;
                curr_needed->y = y;
                curr_needed->next = head;
                head = curr_needed;
                needed++;
            } else if (!curr_sol->needed) {
                curr->shown = false;
            }
        }
    }
    if (params->aggressive) {
        curr_needed = head;
        needed_array = snewn(needed, needed_list_item *);
        memset(needed_array, 0, needed * sizeof(*needed_array));
        i = 0;
        while (curr_needed) {
            needed_array[i] = curr_needed;
            curr_needed = curr_needed->next;
            i++;
        }
        shuffle(needed_array, needed, sizeof(*needed_array), rs);
        for (i = 0; i < needed; i++) {
            curr_needed = needed_array[i];
            curr =
                get_coords(params, desc, curr_needed->x, curr_needed->y);
            if (curr) {
                curr->shown = false;
                if (!solve_check(params, desc, NULL, NULL)) {
#ifdef DEBUG_PRINTS
                    printf("Hiding cell %d, %d not possible.\n",
                           curr_needed->x, curr_needed->y);
#endif
                    curr->shown = true;
                }
                sfree(curr_needed);
                needed_array[i] = NULL;
            }
            curr_needed = NULL;
        }
        sfree(needed_array);
    }
#ifdef DEBUG_PRINTS
    printf("needed %d\n", needed);
#endif
    sfree(sol);
}

static bool start_point_check(size_t size, struct desc_cell *desc)
{
    int i;
    for (i = 0; i < size; i++) {
        if (desc[i].empty || desc[i].full) {
            return true;
        }
    }
    return false;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params, int *x,
                                     int *y, int *w, int *h)
{
    if (ui->cur_visible) {
        *x = COORD_FROM_CELL(ui->cur_x);
        *y = COORD_FROM_CELL(ui->cur_y);
        *w = *h = ds->tilesize;
    }
}

static void generate_image(const game_params *params, random_state *rs,
                           bool *image)
{
    int x, y;
    for (y = 0; y < params->height; y++) {
        for (x = 0; x < params->width; x++) {
            image[(y * params->width) + x] = random_bits(rs, 1);
        }
    }
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive)
{
    bool *image = snewn(params->height * params->width, bool);
    bool valid = false;
    char *desc_string = snewn((params->height * params->width) + 1, char);
    char *compressed_desc =
        snewn((params->height * params->width) + 1, char);
    char space_count;

    struct desc_cell *desc =
        snewn(params->height * params->width, struct desc_cell);
    int x, y, location_in_str;

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

        for (y = 0; y < params->height; y++) {
            for (x = 0; x < params->width; x++) {
                populate_cell(params, image, x, y,
                              x * y == 0 || y == params->height - 1 ||
                              x == params->width - 1,
                              &desc[(y * params->width) + x]);
            }
        }
        valid =
            start_point_check((params->height - 1) * (params->width - 1),
                              desc);
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
    for (y = 0; y < params->height; y++) {
        for (x = 0; x < params->width; x++) {
            if (desc[(y * params->width) + x].shown) {
#ifdef DEBUG_PRINTS
                printf("%d(%d)", desc[(y * params->width) + x].value,
                       desc[(y * params->width) + x].clue);
#endif
                sprintf(desc_string + location_in_str, "%d",
                        desc[(y * params->width) + x].clue);
            } else {
#ifdef DEBUG_PRINTS
                printf("%d( )", desc[(y * params->width) + x].value);
#endif
                sprintf(desc_string + location_in_str, " ");
            }
            location_in_str += 1;
        }
#ifdef DEBUG_PRINTS
        printf("\n");
#endif
    }
    location_in_str = 0;
    space_count = 'a' - 1;
    for (y = 0; y < params->height; y++) {
        for (x = 0; x < params->width; x++) {
            if (desc[(y * params->width) + x].shown) {
                if (space_count >= 'a') {
                    sprintf(compressed_desc + location_in_str, "%c",
                            space_count);
                    location_in_str++;
                    space_count = 'a' - 1;
                }
                sprintf(compressed_desc + location_in_str, "%d",
                        desc[(y * params->width) + x].clue);
                location_in_str++;
            } else {
                if (space_count <= 'z') {
                    space_count++;
                } else {
                    sprintf(compressed_desc + location_in_str, "%c",
                            space_count);
                    location_in_str++;
                    space_count = 'a' - 1;
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

static const char *validate_desc(const game_params *params,
                                 const char *desc)
{
    int size_dest = params->height * params->width;
    int length;
    length = 0;

    while (*desc != '\0') {
        if (*desc >= 'a' && *desc <= 'z') {
            length += *desc - 'a';
        } else if (*desc < '0' || *desc > '9')
            return "Invalid character in game description";
        length++;
        desc++;
    }

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
    state->cells_contents = snewn(params->height * params->width, char);
    memset(state->cells_contents, 0, params->height * params->width);
    state->board = snew(board_state);
    state->board->references = 1;
    state->board->actual_board =
        snewn(params->height * params->width, struct board_cell);

    while (*curr_desc != '\0') {
        if (*curr_desc >= '0' && *curr_desc <= '9') {
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
            while (spaces < total_spaces) {
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
    ret->not_completed_clues = state->not_completed_clues;
    ret->width = state->width;
    ret->height = state->height;
    ret->cells_contents = snewn(state->height * state->width, char);
    memcpy(ret->cells_contents, state->cells_contents,
           state->height * state->width);
    ret->board = state->board;
    ret->board->references++;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->cells_contents);
    state->cells_contents = NULL;
    if (state->board->references <= 1) {
        sfree(state->board->actual_board);
        sfree(state->board);
        state->board = NULL;
    } else {
        state->board->references--;
    }
    sfree(state);
}

static char *solve_game(const game_state *state,
                        const game_state *currstate, const char *aux,
                        const char **error)
{
    struct solution_cell *sol = NULL;
    game_params param;
    bool solved;
    char *ret = NULL;
    unsigned int curr_ret;
    int i, bits, ret_loc = 1;
    int size = state->width * state->height;

    param.width = state->width;
    param.height = state->height;
    solved = solve_game_actual(&param, state->board->actual_board, &sol);
    if (!solved) {
        *error = dupstr("Could not solve this board");
        sfree(sol);
        return NULL;
    }

    ret = snewn((size / 4) + 3, char);

    ret[0] = 's';
    i = 0;
    while (i < size) {
        curr_ret = 0;
        bits = 0;
        while (bits < 8 && i < size) {
            curr_ret <<= 1;
            curr_ret |= sol[i].cell == STATE_MARKED;
            i++;
            bits++;
        }
        curr_ret <<= 8 - bits;
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
    char *desc_string =
        snewn((state->height * state->width) * 3 + 1, char);
    int location_in_str = 0, x, y;
    for (y = 0; y < state->height; y++) {
        for (x = 0; x < state->width; x++) {
            if (state->board->actual_board[(y * state->width) + x].shown) {
                sprintf(desc_string + location_in_str, "|%d|",
                        state->board->actual_board[(y * state->width) +
                                                   x].clue);
            } else {
                sprintf(desc_string + location_in_str, "| |");
            }
            location_in_str += 3;
        }
        sprintf(desc_string + location_in_str, "\n");
        location_in_str += 1;
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
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    char *cell;

    if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible || state->not_completed_clues == 0) return "";
        cell = get_coords(state, state->cells_contents, ui->cur_x, ui->cur_y);
        switch (*cell & STATE_OK_NUM) {
          case STATE_UNMARKED:
            return button == CURSOR_SELECT ? "Black" : "White";
          case STATE_MARKED:
            return button == CURSOR_SELECT ? "White" : "Empty";
          case STATE_BLANK:
            return button == CURSOR_SELECT ? "Empty" : "Black";
        }
    }
    return "";
}

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds, int x, int y,
                            int button)
{
    int srcX = ui->last_x, srcY = ui->last_y;
    int offsetX, offsetY, gameX, gameY, i;
    int dirX, dirY, diff;
    char move_type;
    char move_desc[80];
    char *ret = NULL;
    const char *cell_state;
    bool changed = false;
    if (state->not_completed_clues == 0 && !IS_CURSOR_MOVE(button)) {
        return NULL;
    }
    offsetX = x - (ds->tilesize / 2);
    offsetY = y - (ds->tilesize / 2);
    gameX = offsetX / ds->tilesize;
    gameY = offsetY / ds->tilesize;
    if ((IS_MOUSE_DOWN(button) || IS_MOUSE_DRAG(button) || IS_MOUSE_RELEASE(button))
        && ((offsetX < 0) || (offsetY < 0)))
        return NULL;
    if (button == LEFT_BUTTON || button == RIGHT_BUTTON) {
        cell_state =
            get_coords(state, state->cells_contents, gameX, gameY);
        if (cell_state) {
            ui->last_state = *cell_state & (STATE_BLANK | STATE_MARKED);
            ui->last_state =
                (ui->last_state +
                 ((button ==
                   RIGHT_BUTTON) ? 2 : 1)) % (STATE_BLANK | STATE_MARKED);
        }
        if (button == RIGHT_BUTTON) {
            /* Right button toggles twice */
            move_type = 'T';
        } else {
            move_type = 't';
        }
        if (gameX >= 0 && gameY >= 0 && gameX < state->width &&
            gameY < state->height) {
            sprintf(move_desc, "%c%d,%d", move_type, gameX, gameY);
            ui->last_x = gameX;
            ui->last_y = gameY;
            ret = dupstr(move_desc);
        } else {
            ui->last_x = -1;
            ui->last_y = -1;
        }
        changed = true;
        ui->cur_visible = false;
    } else if (button == LEFT_DRAG || button == RIGHT_DRAG) {
        move_type = 'd';
        /* allowing only drags in straight lines */
        if (gameX >= 0 && gameY >= 0 && gameX < state->width &&
            gameY < state->height && ui->last_x >= 0 && ui->last_y >= 0 &&
            (gameY == ui->last_y || gameX == ui->last_x)) {
            sprintf(move_desc, "%c%d,%d,%d,%d,%d", move_type, gameX, gameY,
                    ui->last_x, ui->last_y, ui->last_state);
            if (srcX == gameX && srcY != gameY) {
                dirX = 0;
                diff = srcY - gameY;
                if (diff < 0) {
                    dirY = -1;
                    diff *= -1;
                } else {
                    dirY = 1;
                }
            } else {
                diff = srcX - gameX;
                dirY = 0;
                if (diff < 0) {
                    dirX = -1;
                    diff *= -1;
                } else {
                    dirX = 1;
                }
            }
            for (i = 0; i < diff; i++) {
                cell_state = get_coords(state, state->cells_contents,
                                        gameX + (dirX * i),
                                        gameY + (dirY * i));
                if (cell_state && (*cell_state & STATE_OK_NUM) == 0
                    && ui->last_state > 0) {
                    changed = true;
                    break;
                }
            }
            ui->last_x = gameX;
            ui->last_y = gameY;
            if (changed) {
                ret = dupstr(move_desc);
            }
        } else {
            ui->last_x = -1;
            ui->last_y = -1;
        }
        ui->cur_visible = false;
    } else if (button == LEFT_RELEASE || button == RIGHT_RELEASE) {
        move_type = 'e';
        if (gameX >= 0 && gameY >= 0 && gameX < state->width &&
            gameY < state->height && ui->last_x >= 0 && ui->last_y >= 0 &&
            (gameY == ui->last_y || gameX == ui->last_x)) {
            sprintf(move_desc, "%c%d,%d,%d,%d,%d", move_type, gameX, gameY,
                    ui->last_x, ui->last_y, ui->last_state);
            if (srcX == gameX && srcY != gameY) {
                dirX = 0;
                diff = srcY - gameY;
                if (diff < 0) {
                    dirY = -1;
                    diff *= -1;
                } else {
                    dirY = 1;
                }
            } else {
                diff = srcX - gameX;
                dirY = 0;
                if (diff < 0) {
                    dirX = -1;
                    diff *= -1;
                } else {
                    dirX = 1;
                }
            }
            for (i = 0; i < diff; i++) {
                cell_state = get_coords(state, state->cells_contents,
                                        gameX + (dirX * i),
                                        gameY + (dirY * i));
                if (cell_state && (*cell_state & STATE_OK_NUM) == 0
                    && ui->last_state > 0) {
                    changed = true;
                    break;
                }
            }
            if (changed) {
                ret = dupstr(move_desc);
            }
        } else {
            ui->last_x = -1;
            ui->last_y = -1;
        }
        ui->cur_visible = false;
    } else if (IS_CURSOR_MOVE(button)) {
        ui->prev_cur_x = ui->cur_x;
        ui->prev_cur_y = ui->cur_y;
        move_cursor(button, &ui->cur_x, &ui->cur_y, state->width,
                    state->height, false);
        ui->cur_visible = true;
        return UI_UPDATE;
    } else if (IS_CURSOR_SELECT(button)) {
        if (!ui->cur_visible) {
            ui->cur_x = 0;
            ui->cur_y = 0;
            ui->cur_visible = true;
            return UI_UPDATE;
        }

        if (button == CURSOR_SELECT2) {
            sprintf(move_desc, "T%d,%d", ui->cur_x, ui->cur_y);
            ret = dupstr(move_desc);
        } else {
            /* Otherwise, treat as LEFT_BUTTON, for a single square. */
            sprintf(move_desc, "t%d,%d", ui->cur_x, ui->cur_y);
            ret = dupstr(move_desc);
        }
    }
    return ret;
}

static void update_board_state_around(game_state *state, int x, int y)
{
    int i, j;
    struct board_cell *curr;
    char *curr_state;
    int total;
    int blank;
    int marked;

    for (i = -1; i < 2; i++) {
        for (j = -1; j < 2; j++) {
            curr =
                get_coords(state, state->board->actual_board, x + i,
                           y + j);
            if (curr && curr->shown) {
                curr_state =
                    get_coords(state, state->cells_contents, x + i, y + j);
                count_around_state(state, x + i, y + j, &marked, &blank,
                                   &total);
                if (curr->clue == marked && (total - marked - blank) == 0) {
                    *curr_state &= STATE_MARKED | STATE_BLANK;
                    *curr_state |= STATE_SOLVED;
                } else if (curr->clue < marked
                           || curr->clue > (total - blank)) {
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
    const char *p;
    char *cell, sol_char;
    int steps = 1, bits, sol_location, dirX, dirY, diff,
        last_state = STATE_UNMARKED;
    unsigned int sol_value;
    struct board_cell *curr_cell;
    char move_type;
    int nparams = 0, move_params[5];

    p = move;
    move_type = *p++;
    switch (move_type) {
      case 't':
      case 'T':
        nparams = 2;
        break;
      case 'd':
      case 'e':
        nparams = 5;
        break;
    }

    for (i = 0; i < nparams; i++) {
        move_params[i] = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
        if (i+1 < nparams) {
            if (*p != ',') {
                free_game(new_state);
                return NULL;
            }
            p++;
        }
    }

    if (move_type == 't' || move_type == 'T') {
        if (move_type == 'T') {
            steps++;
        }
        x = move_params[0];
        y = move_params[1];
        if (x == -1 || y == -1) {
            return new_state;
        }
        cell = get_coords(new_state, new_state->cells_contents, x, y);
        if (cell == NULL) {
            free_game(new_state);
            return NULL;
        }
        if (*cell >= STATE_OK_NUM) {
            *cell &= STATE_OK_NUM;
        }
        *cell = (*cell + steps) % STATE_OK_NUM;
        update_board_state_around(new_state, x, y);
    } else if (move_type == 's') {
        new_state->not_completed_clues = 0;
        new_state->cheating = true;
        sol_location = 0;
        bits = 0;
        i = 1;
        while (i < strlen(move)) {
            sol_value = 0;
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
                if (sol_value & 0x80) {
                    new_state->cells_contents[sol_location] =
                        STATE_MARKED_SOLVED;
                } else {
                    new_state->cells_contents[sol_location] =
                        STATE_BLANK_SOLVED;
                }
                sol_value <<= 1;
                bits--;
                sol_location++;
            }
        }
        return new_state;
    } else if (move_type == 'd' || move_type == 'e') {
        x = move_params[0];
        y = move_params[1];
        srcX = move_params[2];
        srcY = move_params[3];
        last_state = move_params[4];
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
        for (i = 0; i < diff; i++) {
            cell = get_coords(new_state, new_state->cells_contents,
                              x + (dirX * i), y + (dirY * i));
            if (cell == NULL) {
                free_game(new_state);
                return NULL;
            }
            if ((*cell & STATE_OK_NUM) == 0) {
                *cell = last_state;
                update_board_state_around(new_state, x + (dirX * i),
                                          y + (dirY * i));
            }
        }
    }
    for (y = 0; y < state->height; y++) {
        for (x = 0; x < state->width; x++) {
            cell = get_coords(new_state, new_state->cells_contents, x, y);
            curr_cell = get_coords(new_state, new_state->board->actual_board,
                                   x, y);
            if (curr_cell->shown && ((*cell & STATE_SOLVED) == 0)) {
                clues_left++;
            }
        }
    }
    new_state->not_completed_clues = clues_left;
    return new_state;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    *x = (params->width + 1) * tilesize;
    *y = (params->height + 1) * tilesize;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

#define COLOUR(ret, i, r, g, b)                                                \
  ((ret[3 * (i) + 0] = (r)), (ret[3 * (i) + 1] = (g)), (ret[3 * (i) + 2] = (b)))

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);
    COLOUR(ret, COL_GRID, 0.0F, 102 / 255.0F, 99 / 255.0F);
    COLOUR(ret, COL_ERROR, 1.0F, 0.0F, 0.0F);
    COLOUR(ret, COL_BLANK, 236 / 255.0F, 236 / 255.0F, 236 / 255.0F);
    COLOUR(ret, COL_MARKED, 20 / 255.0F, 20 / 255.0F, 20 / 255.0F);
    COLOUR(ret, COL_UNMARKED, 148 / 255.0F, 196 / 255.0F, 190 / 255.0F);
    COLOUR(ret, COL_TEXT_SOLVED, 100 / 255.0F, 100 / 255.0F, 100 / 255.0F);
    COLOUR(ret, COL_CURSOR, 255 / 255.0F, 200 / 255.0F, 200 / 255.0F);

    *ncolours = NCOLOURS;
    return ret;
}

/* Extra flags in game_drawstate entries, not in main game state */
#define DRAWFLAG_CURSOR    0x100
#define DRAWFLAG_CURSOR_U  0x200
#define DRAWFLAG_CURSOR_L  0x400
#define DRAWFLAG_CURSOR_UL 0x800
#define DRAWFLAG_MARGIN_R  0x1000
#define DRAWFLAG_MARGIN_D  0x2000

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    struct game_drawstate *ds = snew(game_drawstate);
    int i;

    ds->tilesize = 0;
    ds->state = NULL;
    ds->state = snewn((state->width + 1) * (state->height + 1), int);
    for (i = 0; i < (state->width + 1) * (state->height + 1); i++)
        ds->state[i] = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->state);
    sfree(ds);
}

static void draw_cell(drawing *dr, int cell, int ts, signed char clue_val,
                      int x, int y)
{
    int startX = ((x * ts) + ts / 2) - 1, startY = ((y * ts) + ts / 2) - 1;
    int color, text_color = COL_TEXT_DARK;

    clip(dr, startX - 1, startY - 1, ts, ts);
    if (!(cell & DRAWFLAG_MARGIN_R))
        draw_rect(dr, startX - 1, startY - 1, ts, 1,
                  (cell & (DRAWFLAG_CURSOR | DRAWFLAG_CURSOR_U) ?
                   COL_CURSOR : COL_GRID));
    if (!(cell & DRAWFLAG_MARGIN_D))
        draw_rect(dr, startX - 1, startY - 1, 1, ts,
                  (cell & (DRAWFLAG_CURSOR | DRAWFLAG_CURSOR_L) ?
                   COL_CURSOR : COL_GRID));
    if (cell & DRAWFLAG_CURSOR_UL)
        draw_rect(dr, startX - 1, startY - 1, 1, 1, COL_CURSOR);

    if (!(cell & (DRAWFLAG_MARGIN_R | DRAWFLAG_MARGIN_D))) {
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

        draw_rect(dr, startX, startY, ts - 1, ts - 1, color);
        if (clue_val >= 0) {
            char clue[80];
            sprintf(clue, "%d", clue_val);
            draw_text(dr, startX + ts / 2, startY + ts / 2, 1, ts * 3 / 5,
                      ALIGN_VCENTRE | ALIGN_HCENTRE, text_color, clue);
        }
    }

    unclip(dr);
    draw_update(dr, startX - 1, startY - 1, ts, ts);
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate,
                        const game_state *state, int dir,
                        const game_ui *ui, float animtime,
                        float flashtime)
{
    int x, y;
    char status[80];
    signed char clue_val;
    bool flashing = (flashtime > 0 && (flashtime <= FLASH_TIME / 3 ||
                                       flashtime > 2*FLASH_TIME / 3));

    for (y = 0; y <= state->height; y++) {
        for (x = 0; x <= state->width; x++) {
            bool inbounds = x < state->width && y < state->height;
            int cell = (inbounds ?
                        state->cells_contents[(y * state->width) + x] : 0);
            if (x == state->width)
                cell |= DRAWFLAG_MARGIN_R;
            if (y == state->height)
                cell |= DRAWFLAG_MARGIN_D;
            if (flashing)
                cell ^= (STATE_BLANK | STATE_MARKED);
            if (ui->cur_visible) {
                if (ui->cur_x == x && ui->cur_y == y)
                    cell |= DRAWFLAG_CURSOR;
                if (ui->cur_x == x-1 && ui->cur_y == y)
                    cell |= DRAWFLAG_CURSOR_L;
                if (ui->cur_x == x && ui->cur_y == y-1)
                    cell |= DRAWFLAG_CURSOR_U;
                if (ui->cur_x == x-1 && ui->cur_y == y-1)
                    cell |= DRAWFLAG_CURSOR_UL;
            }

            if (inbounds &&
                state->board->actual_board[(y * state->width) + x].shown) {
                clue_val = state->board->actual_board[
                    (y * state->width) + x].clue;
            } else {
                clue_val = -1;
            }

            if (ds->state[(y * (state->width+1)) + x] != cell) {
                draw_cell(dr, cell, ds->tilesize, clue_val, x, y);
                ds->state[(y * (state->width+1)) + x] = cell;
            }
        }
    }
    sprintf(status, "Clues left: %d", state->not_completed_clues);
    if (state->not_completed_clues == 0 && !state->cheating) {
        sprintf(status, "COMPLETED!");
    } else if (state->not_completed_clues == 0 && state->cheating) {
        sprintf(status, "Auto solved");
    }
    status_bar(dr, status);
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir,
                              game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir,
                               game_ui *ui)
{
    if (!oldstate->cheating && oldstate->not_completed_clues > 0 &&
        newstate->not_completed_clues == 0) {
        return FLASH_TIME;
    }
    return 0.0F;
}

static int game_status(const game_state *state)
{
    if (state->not_completed_clues == 0)
        return +1;
    return 0;
}

#ifdef COMBINED
#define thegame mosaic
#endif

const struct game thegame = {
    "Mosaic", "games.mosaic", "mosaic",
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
    DEFAULT_TILE_SIZE, game_compute_size, game_set_size,
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
