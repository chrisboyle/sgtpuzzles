/*
 * undead: Implementation of Haunted Mirror Mazes
 *
 * http://www.janko.at/Raetsel/Spukschloss/index.htm
 *
 * Puzzle definition is the total number of each monster type, the
 * grid definition, and the list of sightings (clockwise, starting
 * from top left corner)
 *
 * Example: (Janko puzzle No. 1,
 * http://www.janko.at/Raetsel/Spukschloss/001.a.htm )
 *
 *   Ghosts: 0 Vampires: 2 Zombies: 6
 *
 *     2 1 1 1
 *   1 \ \ . / 2
 *   0 \ . / . 2
 *   0 / . / . 2
 *   3 . . . \ 2
 *     3 3 2 2
 *
 *  would be encoded into: 
 *     4x4:0,2,6,LLaRLaRaRaRdL,2,1,1,1,2,2,2,2,2,2,3,3,3,0,0,1
 *
 *  Additionally, the game description can contain monsters fixed at a
 *  certain grid position. The internal generator does not (yet) use
 *  this feature, but this is needed to enter puzzles like Janko No.
 *  14, which is encoded as:
 *  8x5:12,12,0,LaRbLaRaLaRLbRaVaVaGRaRaRaLbLaRbRLb,0,2,0,2,2,1,2,1,3,1,0,1,8,4,3,0,0,2,3,2,7,2,1,6,2,1
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_TEXT,
    COL_ERROR,
    COL_HIGHLIGHT,
    COL_FLASH,
    COL_GHOST,
    COL_ZOMBIE,
    COL_VAMPIRE,
    COL_DONE,
    NCOLOURS
};

#define DIFFLIST(A)                             \
    A(EASY,Easy,e)                              \
    A(NORMAL,Normal,n)                          \
    A(TRICKY,Tricky,t)
#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const undead_diffnames[] = { DIFFLIST(TITLE) "(count)" };
static char const undead_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

struct game_params {
    int w;      /* Grid width */
    int h;      /* Grid height */
    int diff;   /* Puzzle difficulty */
};

static const struct game_params undead_presets[] = {
    {  4,  4, DIFF_EASY },
    {  4,  4, DIFF_NORMAL },
    {  4,  4, DIFF_TRICKY },
    {  5,  5, DIFF_EASY },
    {  5,  5, DIFF_NORMAL },
    {  5,  5, DIFF_TRICKY },
    {  7,  7, DIFF_EASY },
    {  7,  7, DIFF_NORMAL }
};

#define DEFAULT_PRESET 1

static game_params *default_params(void) {
    game_params *ret = snew(game_params);

    *ret = undead_presets[DEFAULT_PRESET];
    return ret;
}

static bool game_fetch_preset(int i, char **name, game_params **params) {
    game_params *ret;
    char buf[64];

    if (i < 0 || i >= lenof(undead_presets)) return false;

    ret = default_params();
    *ret = undead_presets[i]; /* struct copy */
    *params = ret;

    sprintf(buf, "%dx%d %s",
            undead_presets[i].w, undead_presets[i].h,
            undead_diffnames[undead_presets[i].diff]);
    *name = dupstr(buf);

    return true;
}

static void free_params(game_params *params) {
    sfree(params);
}

static game_params *dup_params(const game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;            /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string) {
    params->w = params->h = atoi(string);

    while (*string && isdigit((unsigned char) *string)) ++string;
    if (*string == 'x') {
        string++;
        params->h = atoi(string);
        while (*string && isdigit((unsigned char)*string)) string++;
    }

    params->diff = DIFF_NORMAL;
    if (*string == 'd') {
        int i;
        string++;
        for (i = 0; i < DIFFCOUNT; i++)
            if (*string == undead_diffchars[i])
                params->diff = i;
        if (*string) string++;
    }

    return;
}

static char *encode_params(const game_params *params, bool full)
{
    char buf[256];
    sprintf(buf, "%dx%d", params->w, params->h);
    if (full)
        sprintf(buf + strlen(buf), "d%c", undead_diffchars[params->diff]);
    return dupstr(buf);
}

static config_item *game_configure(const game_params *params)
{
    config_item *ret;
    char buf[64];

    ret = snewn(4, config_item);

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

    ret[3].name = NULL;
    ret[3].type = C_END;

    return ret;
}

static game_params *custom_params(const config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].u.string.sval);
    ret->h = atoi(cfg[1].u.string.sval);
    ret->diff = cfg[2].u.choices.selected;
    return ret;
}

static const char *validate_params(const game_params *params, bool full)
{
    if (params->w < 3)                  return "Width must be at least 3";
    if (params->h < 3)                  return "Height must be at least 3";
    if (params->w > 54 / params->h)     return "Grid is too big";
    if (params->diff >= DIFFCOUNT)      return "Unknown difficulty rating";
    return NULL;
}

/* --------------------------------------------------------------- */
/* Game state allocation, deallocation. */

struct path {
    int length;
    int *p;
    int grid_start;
    int grid_end;
    int num_monsters;
    int *mapping;
    int sightings_start;
    int sightings_end;
    int *xy;
};

struct game_common {
    int refcount;
    struct game_params params;
    int wh;
    int num_ghosts,num_vampires,num_zombies,num_total;
    int num_paths;
    struct path *paths;
    int *grid;
    int *xinfo;
    bool *fixed;
};

struct game_state {
    struct game_common *common;
    int *guess;
    unsigned char *pencils;
    bool *cell_errors;
    bool *hint_errors;
    bool *hints_done;
    bool count_errors[3];
    bool solved;
    bool cheated;
};

static game_state *new_state(const game_params *params) {
    int i;
    game_state *state = snew(game_state);
    state->common = snew(struct game_common);

    state->common->refcount = 1;
    state->common->params.w = params->w;
    state->common->params.h = params->h;
    state->common->params.diff = params->diff;

    state->common->wh = (state->common->params.w +2) * (state->common->params.h +2);

    state->common->num_ghosts = 0;
    state->common->num_vampires = 0;
    state->common->num_zombies = 0;
    state->common->num_total = 0;

    state->common->grid = snewn(state->common->wh, int);
    state->common->xinfo = snewn(state->common->wh, int);
    state->common->fixed = NULL;

    state->common->num_paths =
        state->common->params.w + state->common->params.h;
    state->common->paths = snewn(state->common->num_paths, struct path);

    for (i=0;i<state->common->num_paths;i++) {
        state->common->paths[i].length = 0;
        state->common->paths[i].grid_start = -1;
        state->common->paths[i].grid_end = -1;
        state->common->paths[i].num_monsters = 0;
        state->common->paths[i].sightings_start = 0;
        state->common->paths[i].sightings_end = 0;
        state->common->paths[i].p = snewn(state->common->wh,int);
        state->common->paths[i].xy = snewn(state->common->wh,int);
        state->common->paths[i].mapping = snewn(state->common->wh,int);
    }

    state->guess = NULL;
    state->pencils = NULL;

    state->cell_errors = snewn(state->common->wh, bool);
    for (i=0;i<state->common->wh;i++)
        state->cell_errors[i] = false;
    state->hint_errors = snewn(2*state->common->num_paths, bool);
    for (i=0;i<2*state->common->num_paths;i++)
        state->hint_errors[i] = false;
    state->hints_done = snewn(2 * state->common->num_paths, bool);
    memset(state->hints_done, 0,
           2 * state->common->num_paths * sizeof(bool));
    for (i=0;i<3;i++)
        state->count_errors[i] = false;

    state->solved = false;
    state->cheated = false;
    
    return state;
}

static game_state *dup_game(const game_state *state)
{
    game_state *ret = snew(game_state);

    ret->common = state->common;
    ret->common->refcount++;

    if (state->guess != NULL) {
        ret->guess = snewn(ret->common->num_total,int);
        memcpy(ret->guess, state->guess, ret->common->num_total*sizeof(int));
    }
    else ret->guess = NULL;

    if (state->pencils != NULL) {
        ret->pencils = snewn(ret->common->num_total,unsigned char);
        memcpy(ret->pencils, state->pencils,
               ret->common->num_total*sizeof(unsigned char));
    }
    else ret->pencils = NULL;

    if (state->cell_errors != NULL) {
        ret->cell_errors = snewn(ret->common->wh,bool);
        memcpy(ret->cell_errors, state->cell_errors,
               ret->common->wh*sizeof(bool));
    }
    else ret->cell_errors = NULL;

    if (state->hint_errors != NULL) {
        ret->hint_errors = snewn(2*ret->common->num_paths,bool);
        memcpy(ret->hint_errors, state->hint_errors,
               2*ret->common->num_paths*sizeof(bool));
    }
    else ret->hint_errors = NULL;

    if (state->hints_done != NULL) {
        ret->hints_done = snewn(2 * state->common->num_paths, bool);
        memcpy(ret->hints_done, state->hints_done,
               2 * state->common->num_paths * sizeof(bool));
    }
    else ret->hints_done = NULL;

    ret->count_errors[0] = state->count_errors[0];
    ret->count_errors[1] = state->count_errors[1];
    ret->count_errors[2] = state->count_errors[2];

    ret->solved = state->solved;
    ret->cheated = state->cheated;
    
    return ret;
}

static void free_game(game_state *state) {
    int i;

    state->common->refcount--;
    if (state->common->refcount == 0) {
        for (i=0;i<state->common->num_paths;i++) {
            sfree(state->common->paths[i].mapping);
            sfree(state->common->paths[i].xy);
            sfree(state->common->paths[i].p);
        }
        sfree(state->common->paths);
        sfree(state->common->xinfo);
        sfree(state->common->grid);
        if (state->common->fixed != NULL) sfree(state->common->fixed);
        sfree(state->common);
    }
    if (state->hints_done != NULL) sfree(state->hints_done);
    if (state->hint_errors != NULL) sfree(state->hint_errors);
    if (state->cell_errors != NULL) sfree(state->cell_errors);
    if (state->pencils != NULL) sfree(state->pencils);
    if (state->guess != NULL) sfree(state->guess);
    sfree(state);

    return;
}

/* --------------------------------------------------------------- */
/* Puzzle generator */

/* cell states */
enum {
    CELL_EMPTY,
    CELL_MIRROR_L,
    CELL_MIRROR_R,
    CELL_GHOST,
    CELL_VAMPIRE,
    CELL_ZOMBIE,
    CELL_UNDEF
};

/* grid walk directions */
enum {
    DIRECTION_NONE,
    DIRECTION_UP,
    DIRECTION_RIGHT,
    DIRECTION_LEFT,
    DIRECTION_DOWN
};

static int range2grid(int rangeno, int width, int height, int *x, int *y) {

    if (rangeno < 0) {
        *x = 0; *y = 0; return DIRECTION_NONE;
    }
    if (rangeno < width) {
        *x = rangeno+1; *y = 0; return DIRECTION_DOWN;
    }
    rangeno = rangeno - width;
    if (rangeno < height) {
        *x = width+1; *y = rangeno+1; return DIRECTION_LEFT;
    }
    rangeno = rangeno - height;
    if (rangeno < width) {
        *x = width-rangeno; *y = height+1; return DIRECTION_UP;
    }
    rangeno = rangeno - width;
    if (rangeno < height) {
        *x = 0; *y = height-rangeno; return DIRECTION_RIGHT;
    }
    *x = 0; *y = 0;
    return DIRECTION_NONE;
}

static int grid2range(int x, int y, int w, int h) {
    if (x>0 && x<w+1 && y>0 && y<h+1)           return -1;
    if (x<0 || x>w+1 || y<0 || y>h+1)           return -1;
    if ((x == 0 || x==w+1) && (y==0 || y==h+1)) return -1;
    if (y==0)                                   return x-1;
    if (x==(w+1))                               return y-1+w;
    if (y==(h+1))                               return 2*w + h - x;
    return 2*(w+h) - y;
}

static void make_paths(game_state *state) {
    int i;
    int count = 0;

    for (i=0;i<2*(state->common->params.w + state->common->params.h);i++) {
        int x,y,dir;
        int j,k,num_monsters;
        bool found;
        int c,p; 
        found = false;
        /* Check whether inverse path is already in list */
        for (j=0;j<count;j++) {
            if (i == state->common->paths[j].grid_end) {
                found = true;
                break;
            }
        }
        if (found) continue;

        /* We found a new path through the mirror maze */
        state->common->paths[count].grid_start = i;     
        dir = range2grid(i, state->common->params.w,
                         state->common->params.h,&x,&y);     
        state->common->paths[count].sightings_start =
            state->common->grid[x+y*(state->common->params.w +2)];
        while (true) {
            int c,r;

            if      (dir == DIRECTION_DOWN)     y++;
            else if (dir == DIRECTION_LEFT)     x--;
            else if (dir == DIRECTION_UP)       y--;
            else if (dir == DIRECTION_RIGHT)    x++;
            
            r = grid2range(x, y, state->common->params.w,
                           state->common->params.h);
            if (r != -1) {
                state->common->paths[count].grid_end = r;               
                state->common->paths[count].sightings_end =
                    state->common->grid[x+y*(state->common->params.w +2)];
                break;
            }

            c = state->common->grid[x+y*(state->common->params.w+2)];
            state->common->paths[count].xy[state->common->paths[count].length] =
                x+y*(state->common->params.w+2);
            if (c == CELL_MIRROR_L) {
                state->common->paths[count].p[state->common->paths[count].length] = -1;
                if (dir == DIRECTION_DOWN)          dir = DIRECTION_RIGHT;
                else if (dir == DIRECTION_LEFT)     dir = DIRECTION_UP;
                else if (dir == DIRECTION_UP)       dir = DIRECTION_LEFT;
                else if (dir == DIRECTION_RIGHT)    dir = DIRECTION_DOWN;
            }
            else if (c == CELL_MIRROR_R) {
                state->common->paths[count].p[state->common->paths[count].length] = -1;
                if (dir == DIRECTION_DOWN)          dir = DIRECTION_LEFT;
                else if (dir == DIRECTION_LEFT)     dir = DIRECTION_DOWN;
                else if (dir == DIRECTION_UP)       dir = DIRECTION_RIGHT;
                else if (dir == DIRECTION_RIGHT)    dir = DIRECTION_UP;
            }
            else {
                state->common->paths[count].p[state->common->paths[count].length] =
                    state->common->xinfo[x+y*(state->common->params.w+2)];
            }
            state->common->paths[count].length++;
        }
        /* Count unique monster entries in each path */
        state->common->paths[count].num_monsters = 0;
        for (j=0;j<state->common->num_total;j++) {
            num_monsters = 0;
            for (k=0;k<state->common->paths[count].length;k++)
                if (state->common->paths[count].p[k] == j)
                    num_monsters++;
            if (num_monsters > 0)
                state->common->paths[count].num_monsters++;
        }

        /* Generate mapping vector */
        c = 0;
        for (p=0;p<state->common->paths[count].length;p++) {
            int m;
            m = state->common->paths[count].p[p];
            if (m == -1) continue;
            found = false;
            for (j=0; j<c; j++)
                if (state->common->paths[count].mapping[j] == m) found = true;
            if (!found) state->common->paths[count].mapping[c++] = m;
        }
        count++;
    }
    return;
}

struct guess {
    int length;
    int *guess;
    int *possible;
};

static bool next_list(struct guess *g, int pos) {

    if (pos == 0) {
        if ((g->guess[pos] == 1 && g->possible[pos] == 1) || 
            (g->guess[pos] == 2 && (g->possible[pos] == 3 ||
                                    g->possible[pos] == 2)) ||
            g->guess[pos] == 4)
            return false;
        if (g->guess[pos] == 1 && (g->possible[pos] == 3 ||
                                   g->possible[pos] == 7)) {
            g->guess[pos] = 2; return true;
        }
        if (g->guess[pos] == 1 && g->possible[pos] == 5) {
            g->guess[pos] = 4; return true;
        }
        if (g->guess[pos] == 2 && (g->possible[pos] == 6 || g->possible[pos] == 7)) {
            g->guess[pos] = 4; return true;
        }
    }

    if (g->guess[pos] == 1) {
        if (g->possible[pos] == 1) {
            return next_list(g,pos-1);
        }
        if (g->possible[pos] == 3 || g->possible[pos] == 7) {
            g->guess[pos] = 2; return true;
        }
        if (g->possible[pos] == 5) {
            g->guess[pos] = 4; return true;
        }
    }

    if (g->guess[pos] == 2) {
        if (g->possible[pos] == 2) {
            return next_list(g,pos-1);
        }
        if (g->possible[pos] == 3) {
            g->guess[pos] = 1; return next_list(g,pos-1);
        }
        if (g->possible[pos] == 6 || g->possible[pos] == 7) {
            g->guess[pos] = 4; return true;
        }
    }

    if (g->guess[pos] == 4) {
        if (g->possible[pos] == 5 || g->possible[pos] == 7) {
            g->guess[pos] = 1; return next_list(g,pos-1);
        }
        if (g->possible[pos] == 6) {
            g->guess[pos] = 2; return next_list(g,pos-1);
        }
        if (g->possible[pos] == 4) {
            return next_list(g,pos-1);
        }
    }
    return false;
}

static void get_unique(game_state *state, int counter, random_state *rs) {

    int p,i,c,pathlimit,count_uniques;
    struct guess path_guess;
    int *view_count;
    
    struct entry {
        struct entry *link;
        int *guess;
        int start_view;
        int end_view;
    };

    struct {
        struct entry *head;
        struct entry *node;
    } views, single_views, test_views;

    struct entry test_entry;

    path_guess.length = state->common->paths[counter].num_monsters;
    path_guess.guess = snewn(path_guess.length,int);
    path_guess.possible = snewn(path_guess.length,int);
    for (i=0;i<path_guess.length;i++) 
        path_guess.guess[i] = path_guess.possible[i] = 0;

    for (p=0;p<path_guess.length;p++) {
        path_guess.possible[p] =
            state->guess[state->common->paths[counter].mapping[p]];
        switch (path_guess.possible[p]) {
          case 1: path_guess.guess[p] = 1; break;
          case 2: path_guess.guess[p] = 2; break;
          case 3: path_guess.guess[p] = 1; break;
          case 4: path_guess.guess[p] = 4; break;
          case 5: path_guess.guess[p] = 1; break;
          case 6: path_guess.guess[p] = 2; break;
          case 7: path_guess.guess[p] = 1; break;
        }
    }

    views.head = NULL;
    views.node = NULL;

    pathlimit = state->common->paths[counter].length + 1;
    view_count = snewn(pathlimit*pathlimit, int);
    for (i = 0; i < pathlimit*pathlimit; i++)
        view_count[i] = 0;
    
    do {
        bool mirror;
        int start_view, end_view;
        
        mirror = false;
        start_view = 0;
        for (p=0;p<state->common->paths[counter].length;p++) {
            if (state->common->paths[counter].p[p] == -1) mirror = true;
            else {
                for (i=0;i<path_guess.length;i++) {
                    if (state->common->paths[counter].p[p] ==
                        state->common->paths[counter].mapping[i]) {
                        if (path_guess.guess[i] == 1 && mirror)
                            start_view++;
                        if (path_guess.guess[i] == 2 && !mirror)
                            start_view++;
                        if (path_guess.guess[i] == 4)
                            start_view++;
                        break;
                    }
                }
            }
        }
        mirror = false;
        end_view = 0;
        for (p=state->common->paths[counter].length-1;p>=0;p--) {
            if (state->common->paths[counter].p[p] == -1) mirror = true;
            else {
                for (i=0;i<path_guess.length;i++) {
                    if (state->common->paths[counter].p[p] ==
                        state->common->paths[counter].mapping[i]) {
                        if (path_guess.guess[i] == 1 && mirror)
                            end_view++;
                        if (path_guess.guess[i] == 2 && !mirror)
                            end_view++;
                        if (path_guess.guess[i] == 4)
                            end_view++;
                        break;
                    }
                }
            }
        }

        assert(start_view >= 0 && start_view < pathlimit);
        assert(end_view >= 0 && end_view < pathlimit);
        i = start_view * pathlimit + end_view;
        view_count[i]++;
        if (view_count[i] == 1) {
            views.node = snewn(1,struct entry);
            views.node->link = views.head;
            views.node->guess = snewn(path_guess.length,int);
            views.head = views.node;
            views.node->start_view = start_view;
            views.node->end_view = end_view;
            memcpy(views.node->guess, path_guess.guess,
                   path_guess.length*sizeof(int));
        }
    } while (next_list(&path_guess, path_guess.length-1));

    /*  extract single entries from view list */

    test_views.head = views.head;
    test_views.node = views.node;
    
    test_entry.guess = snewn(path_guess.length,int);

    single_views.head = NULL;
    single_views.node = NULL;

    count_uniques = 0;
    while (test_views.head != NULL) {
        test_views.node = test_views.head;
        test_views.head = test_views.head->link;
        i = test_views.node->start_view * pathlimit + test_views.node->end_view;
        if (view_count[i] == 1) {
            single_views.node = snewn(1,struct entry);
            single_views.node->link = single_views.head;
            single_views.node->guess = snewn(path_guess.length,int);
            single_views.head = single_views.node;
            single_views.node->start_view = test_views.node->start_view;
            single_views.node->end_view = test_views.node->end_view;
            memcpy(single_views.node->guess, test_views.node->guess,
                   path_guess.length*sizeof(int));
            count_uniques++;
        }
    }

    sfree(view_count);

    if (count_uniques > 0) {
        test_entry.start_view = 0;
        test_entry.end_view = 0;
        /* Choose one unique guess per random */
        /* While we are busy with looping through single_views, we
         * conveniently free the linked list single_view */
        c = random_upto(rs,count_uniques);
        while(single_views.head != NULL) {
            single_views.node = single_views.head;
            single_views.head = single_views.head->link;
            if (c-- == 0) {
                memcpy(test_entry.guess, single_views.node->guess,
                       path_guess.length*sizeof(int));
                test_entry.start_view = single_views.node->start_view;
                test_entry.end_view = single_views.node->end_view;
            }
            sfree(single_views.node->guess);
            sfree(single_views.node);
        }
        
        /* Modify state_guess according to path_guess.mapping */
        for (i=0;i<path_guess.length;i++)
            state->guess[state->common->paths[counter].mapping[i]] =
                test_entry.guess[i];
    }

    sfree(test_entry.guess);

    while (views.head != NULL) {
        views.node = views.head;
        views.head = views.head->link;
        sfree(views.node->guess);
        sfree(views.node);
    }

    sfree(path_guess.possible);
    sfree(path_guess.guess);

    return;
}

static int count_monsters(game_state *state,
                          int *cGhost, int *cVampire, int *cZombie) {
    int cNone;
    int i;

    *cGhost = *cVampire = *cZombie = cNone = 0;

    for (i=0;i<state->common->num_total;i++) {
        if (state->guess[i] == 1) (*cGhost)++;
        else if (state->guess[i] == 2) (*cVampire)++;
        else if (state->guess[i] == 4) (*cZombie)++;
        else cNone++;
    }

    return cNone;
}

static bool check_numbers(game_state *state, int *guess) {
    bool valid;
    int i;
    int count_ghosts, count_vampires, count_zombies;

    count_ghosts = count_vampires = count_zombies = 0;  
    for (i=0;i<state->common->num_total;i++) {
        if (guess[i] == 1) count_ghosts++;
        if (guess[i] == 2) count_vampires++;
        if (guess[i] == 4) count_zombies++;
    }

    valid = true;

    if (count_ghosts   > state->common->num_ghosts)   valid = false; 
    if (count_vampires > state->common->num_vampires) valid = false; 
    if (count_zombies > state->common->num_zombies)   valid = false; 

    return valid;
}

static bool check_solution(int *g, struct path path) {
    int i;
    bool mirror;
    int count;

    count = 0;
    mirror = false;
    for (i=0;i<path.length;i++) {
        if (path.p[i] == -1) mirror = true;
        else {
            if (g[path.p[i]] == 1 && mirror) count++;
            else if (g[path.p[i]] == 2 && !mirror) count++;
            else if (g[path.p[i]] == 4) count++;
        }
    }
    if (count != path.sightings_start) return false;    

    count = 0;
    mirror = false;
    for (i=path.length-1;i>=0;i--) {
        if (path.p[i] == -1) mirror = true;
        else {
            if (g[path.p[i]] == 1 && mirror) count++;
            else if (g[path.p[i]] == 2 && !mirror) count++;
            else if (g[path.p[i]] == 4) count++;
        }
    }
    if (count != path.sightings_end) return false;  

    return true;
}

static bool solve_iterative(game_state *state, struct path *paths) {
    bool solved;
    int p,i,j,count;

    int *guess;
    int *possible;

    struct guess loop;

    solved = true;
    loop.length = state->common->num_total;
    guess = snewn(state->common->num_total,int);
    possible = snewn(state->common->num_total,int);

    for (i=0;i<state->common->num_total;i++) {
        guess[i] = state->guess[i];
        possible[i] = 0;
    }

    for (p=0;p<state->common->num_paths;p++) {
        if (paths[p].num_monsters > 0) {
            loop.length = paths[p].num_monsters;
            loop.guess = snewn(paths[p].num_monsters,int);
            loop.possible = snewn(paths[p].num_monsters,int);

            for (i=0;i<paths[p].num_monsters;i++) {
                switch (state->guess[paths[p].mapping[i]]) {
                  case 1: loop.guess[i] = 1; break;
                  case 2: loop.guess[i] = 2; break;
                  case 3: loop.guess[i] = 1; break;
                  case 4: loop.guess[i] = 4; break;
                  case 5: loop.guess[i] = 1; break;
                  case 6: loop.guess[i] = 2; break;
                  case 7: loop.guess[i] = 1; break;
                }
                loop.possible[i] = state->guess[paths[p].mapping[i]];
                possible[paths[p].mapping[i]] = 0;
            }

            while(true) {
                for (i=0;i<state->common->num_total;i++) {
                    guess[i] = state->guess[i];
                }
                count = 0;
                for (i=0;i<paths[p].num_monsters;i++) 
                    guess[paths[p].mapping[i]] = loop.guess[count++];
                if (check_numbers(state,guess) &&
                    check_solution(guess,paths[p]))
                    for (j=0;j<paths[p].num_monsters;j++)
                        possible[paths[p].mapping[j]] |= loop.guess[j];
                if (!next_list(&loop,loop.length-1)) break;
            }
            for (i=0;i<paths[p].num_monsters;i++)       
                state->guess[paths[p].mapping[i]] &=
                    possible[paths[p].mapping[i]];
            sfree(loop.possible);
            sfree(loop.guess);
        }
    }

    for (i=0;i<state->common->num_total;i++) {
        if (state->guess[i] == 3 || state->guess[i] == 5 ||
            state->guess[i] == 6 || state->guess[i] == 7) {
            solved = false; break;
        }
    }

    sfree(possible);
    sfree(guess);

    return solved;
}

static bool solve_bruteforce(game_state *state, struct path *paths) {
    bool solved, correct;
    int number_solutions;
    int p,i;

    struct guess loop;

    loop.guess = snewn(state->common->num_total,int);
    loop.possible = snewn(state->common->num_total,int);

    for (i=0;i<state->common->num_total;i++) {
        loop.possible[i] = state->guess[i];
        switch (state->guess[i]) {
          case 1: loop.guess[i] = 1; break;
          case 2: loop.guess[i] = 2; break;
          case 3: loop.guess[i] = 1; break;
          case 4: loop.guess[i] = 4; break;
          case 5: loop.guess[i] = 1; break;
          case 6: loop.guess[i] = 2; break;
          case 7: loop.guess[i] = 1; break;
        }
    }

    solved = false;
    number_solutions = 0;

    while (true) {

        correct = true;
        if (!check_numbers(state,loop.guess)) correct = false;
        else 
            for (p=0;p<state->common->num_paths;p++)
                if (!check_solution(loop.guess,paths[p])) {
                    correct = false; break;
                }
        if (correct) {
            number_solutions++;
            solved = true;
            if(number_solutions > 1) {
                solved = false;
                break; 
            }
            for (i=0;i<state->common->num_total;i++)
                state->guess[i] = loop.guess[i];
        }
        if (!next_list(&loop,state->common->num_total -1)) {
            break;
        }
    }

    sfree(loop.possible);
    sfree(loop.guess);

    return solved;
}

static int path_cmp(const void *a, const void *b) {
    const struct path *pa = (const struct path *)a;
    const struct path *pb = (const struct path *)b;
    return pa->num_monsters - pb->num_monsters;
}

static char *new_game_desc(const game_params *params, random_state *rs,
                           char **aux, bool interactive) {
    int i,count,c,w,h,r,p,g;
    game_state *new;

    /* Variables for puzzle generation algorithm */
    int filling;
    int max_length;
    int count_ghosts, count_vampires, count_zombies;
    bool abort;
    float ratio;
    
    /* Variables for solver algorithm */
    bool solved_iterative, solved_bruteforce, contains_inconsistency;
    int count_ambiguous;
    int iterative_depth;
    int *old_guess;

    /* Variables for game description generation */
    int x,y;
    char *e;
    char *desc; 

    i = 0;
    while (true) {
        new = new_state(params);
        abort = false;

        /* Fill grid with random mirrors and (later to be populated)
         * empty monster cells */
        count = 0;
        for (h=1;h<new->common->params.h+1;h++)
            for (w=1;w<new->common->params.w+1;w++) {
                c = random_upto(rs,5);
                if (c >= 2) {
                    new->common->grid[w+h*(new->common->params.w+2)] = CELL_EMPTY;
                    new->common->xinfo[w+h*(new->common->params.w+2)] = count++;
                }
                else if (c == 0) {
                    new->common->grid[w+h*(new->common->params.w+2)] = 
                        CELL_MIRROR_L;
                    new->common->xinfo[w+h*(new->common->params.w+2)] = -1;
                }
                else {
                    new->common->grid[w+h*(new->common->params.w+2)] =
                        CELL_MIRROR_R;
                    new->common->xinfo[w+h*(new->common->params.w+2)] = -1;         
                }
            }
        new->common->num_total = count; /* Total number of monsters in maze */

        /* Puzzle is boring if it has too few monster cells. Discard
         * grid, make new grid */
        if (new->common->num_total <= 4) {
            free_game(new);
            continue;
        }

        /* Monsters / Mirrors ratio should be balanced */
        ratio = (float)new->common->num_total /
            (float)(new->common->params.w * new->common->params.h);
        if (ratio < 0.48F || ratio > 0.78F) {
            free_game(new);
            continue;
        }        

        /* Assign clue identifiers */   
        for (r=0;r<2*(new->common->params.w+new->common->params.h);r++) {
            int x,y,gridno;
            gridno = range2grid(r,new->common->params.w,new->common->params.h,
                                &x,&y);
            new->common->grid[x+y*(new->common->params.w +2)] = gridno;
            new->common->xinfo[x+y*(new->common->params.w +2)] = 0;
        }
        /* The four corners don't matter at all for the game. Set them
         * all to zero, just to have a nice data structure */
        new->common->grid[0] = 0;        
        new->common->xinfo[0] = 0;      
        new->common->grid[new->common->params.w+1] = 0; 
        new->common->xinfo[new->common->params.w+1] = 0;
        new->common->grid[new->common->params.w+1 + (new->common->params.h+1)*(new->common->params.w+2)] = 0; 
        new->common->xinfo[new->common->params.w+1 + (new->common->params.h+1)*(new->common->params.w+2)] = 0;
        new->common->grid[(new->common->params.h+1)*(new->common->params.w+2)] = 0; 
        new->common->xinfo[(new->common->params.h+1)*(new->common->params.w+2)] = 0;

        /* Initialize solution vector */
        new->guess = snewn(new->common->num_total,int);
        for (g=0;g<new->common->num_total;g++) new->guess[g] = 7;

        /* Initialize fixed flag from common. Not needed for the
         * puzzle generator; initialize it for having clean code */
        new->common->fixed = snewn(new->common->num_total, bool);
        for (g=0;g<new->common->num_total;g++)
            new->common->fixed[g] = false;

        /* paths generation */
        make_paths(new);

        /* Grid is invalid if max. path length > threshold. Discard
         * grid, make new one */
        switch (new->common->params.diff) {
          case DIFF_EASY:     max_length = min(new->common->params.w,new->common->params.h) + 1; break;
          case DIFF_NORMAL:   max_length = (max(new->common->params.w,new->common->params.h) * 3) / 2; break;
          case DIFF_TRICKY:   max_length = 9; break;
          default:            max_length = 9; break;
        }

        for (p=0;p<new->common->num_paths;p++) {
            if (new->common->paths[p].num_monsters > max_length) {
                abort = true;
            }
        }
        if (abort) {
            free_game(new);
            continue;
        }

        qsort(new->common->paths, new->common->num_paths,
              sizeof(struct path), path_cmp);

        /* Grid monster initialization */
        /*  For easy puzzles, we try to fill nearly the whole grid
            with unique solution paths (up to 2) For more difficult
            puzzles, we fill only roughly half the grid, and choose
            random monsters for the rest For hard puzzles, we fill
            even less paths with unique solutions */

        switch (new->common->params.diff) {
          case DIFF_EASY:   filling = 2; break;
          case DIFF_NORMAL: filling = min( (new->common->params.w+new->common->params.h) , (new->common->num_total)/2 ); break;
          case DIFF_TRICKY: filling = max( (new->common->params.w+new->common->params.h) , (new->common->num_total)/2 ); break;
          default:          filling = 0; break;
        }

        count = 0;
        while ( (count_monsters(new, &count_ghosts, &count_vampires,
                                &count_zombies)) > filling) {
            if ((count) >= new->common->num_paths) break;
            if (new->common->paths[count].num_monsters == 0) {
                count++;
                continue;
            }
            get_unique(new,count,rs);
            count++;
        }

        /* Fill any remaining ambiguous entries with random monsters */ 
        for(g=0;g<new->common->num_total;g++) {
            if (new->guess[g] == 7) {
                r = random_upto(rs,3);
                new->guess[g] = (r == 0) ? 1 : ( (r == 1) ? 2 : 4 );        
            }
        }

        /*  Determine all hints */
        count_monsters(new, &new->common->num_ghosts,
                       &new->common->num_vampires, &new->common->num_zombies);

        /* Puzzle is trivial if it has only one type of monster. Discard. */
        if ((new->common->num_ghosts == 0 && new->common->num_vampires == 0) ||
            (new->common->num_ghosts == 0 && new->common->num_zombies == 0) ||
            (new->common->num_vampires == 0 && new->common->num_zombies == 0)) {
            free_game(new);
            continue;
        }

        /* Discard puzzle if difficulty Tricky, and it has only 1
         * member of any monster type */
        if (new->common->params.diff == DIFF_TRICKY && 
            (new->common->num_ghosts <= 1 ||
             new->common->num_vampires <= 1 || new->common->num_zombies <= 1)) {
            free_game(new);
            continue;
        }

        for (w=1;w<new->common->params.w+1;w++)
            for (h=1;h<new->common->params.h+1;h++) {
                c = new->common->xinfo[w+h*(new->common->params.w+2)];
                if (c >= 0) {
                    if (new->guess[c] == 1) new->common->grid[w+h*(new->common->params.w+2)] = CELL_GHOST;
                    if (new->guess[c] == 2) new->common->grid[w+h*(new->common->params.w+2)] = CELL_VAMPIRE;
                    if (new->guess[c] == 4) new->common->grid[w+h*(new->common->params.w+2)] = CELL_ZOMBIE;                 
                }
            }

        /* Prepare path information needed by the solver (containing all hints) */  
        for (p=0;p<new->common->num_paths;p++) {
            bool mirror;
            int x,y;

            new->common->paths[p].sightings_start = 0;
            new->common->paths[p].sightings_end = 0;
            
            mirror = false;
            for (g=0;g<new->common->paths[p].length;g++) {

                if (new->common->paths[p].p[g] == -1) mirror = true;
                else {
                    if      (new->guess[new->common->paths[p].p[g]] == 1 && mirror)  (new->common->paths[p].sightings_start)++;
                    else if (new->guess[new->common->paths[p].p[g]] == 2 && !mirror) (new->common->paths[p].sightings_start)++;
                    else if (new->guess[new->common->paths[p].p[g]] == 4)                    (new->common->paths[p].sightings_start)++;
                }
            }

            mirror = false;
            for (g=new->common->paths[p].length-1;g>=0;g--) {
                if (new->common->paths[p].p[g] == -1) mirror = true;
                else {
                    if      (new->guess[new->common->paths[p].p[g]] == 1 && mirror)  (new->common->paths[p].sightings_end)++;
                    else if (new->guess[new->common->paths[p].p[g]] == 2 && !mirror) (new->common->paths[p].sightings_end)++;
                    else if (new->guess[new->common->paths[p].p[g]] == 4)                    (new->common->paths[p].sightings_end)++;
                }
            }

            range2grid(new->common->paths[p].grid_start,
                       new->common->params.w,new->common->params.h,&x,&y);
            new->common->grid[x+y*(new->common->params.w +2)] =
                new->common->paths[p].sightings_start;
            range2grid(new->common->paths[p].grid_end,
                       new->common->params.w,new->common->params.h,&x,&y);
            new->common->grid[x+y*(new->common->params.w +2)] =
                new->common->paths[p].sightings_end;
        }

        /* Try to solve the puzzle with the iterative solver */
        old_guess = snewn(new->common->num_total,int);
        for (p=0;p<new->common->num_total;p++) {
            new->guess[p] = 7;
            old_guess[p] = 7;
        }
        iterative_depth = 0;
        solved_iterative = false;
        contains_inconsistency = false;
        count_ambiguous = 0;

        while (true) {
            bool no_change = true;
            solved_iterative = solve_iterative(new,new->common->paths);
            iterative_depth++;      
            for (p=0;p<new->common->num_total;p++) {
                if (new->guess[p] != old_guess[p]) no_change = false;
                old_guess[p] = new->guess[p];
                if (new->guess[p] == 0) contains_inconsistency = true;
            }
            if (solved_iterative || no_change) break;
        } 

        /* If necessary, try to solve the puzzle with the brute-force solver */
        solved_bruteforce = false;  
        if (new->common->params.diff != DIFF_EASY &&
            !solved_iterative && !contains_inconsistency) {
            for (p=0;p<new->common->num_total;p++)
                if (new->guess[p] != 1 && new->guess[p] != 2 &&
                    new->guess[p] != 4) count_ambiguous++;

            solved_bruteforce = solve_bruteforce(new, new->common->paths);
        }   

        /*  Determine puzzle difficulty level */    
        if (new->common->params.diff == DIFF_EASY && solved_iterative &&
            iterative_depth <= 3 && !contains_inconsistency) { 
/*          printf("Puzzle level: EASY Level %d Ratio %f Ambiguous %d (Found after %i tries)\n",iterative_depth, ratio, count_ambiguous, i); */
            break;
        }

        if (new->common->params.diff == DIFF_NORMAL &&
            ((solved_iterative && iterative_depth > 3) ||
             (solved_bruteforce && count_ambiguous < 4)) &&
            !contains_inconsistency) {  
/*          printf("Puzzle level: NORMAL Level %d Ratio %f Ambiguous %d (Found after %d tries)\n", iterative_depth, ratio, count_ambiguous, i); */
            break;
        }
        if (new->common->params.diff == DIFF_TRICKY &&
            solved_bruteforce && iterative_depth > 0 &&
            count_ambiguous >= 4 && !contains_inconsistency) {
/*          printf("Puzzle level: TRICKY Level %d Ratio %f Ambiguous %d (Found after %d tries)\n", iterative_depth, ratio, count_ambiguous, i); */
            break;
        }

        /* If puzzle is not solvable or does not satisfy the desired
         * difficulty level, free memory and start from scratch */    
        sfree(old_guess);
        free_game(new);
        i++;
    }
    
    /* We have a valid puzzle! */
    
    desc = snewn(10 + new->common->wh +
                 6*(new->common->params.w + new->common->params.h), char);
    e = desc;

    /* Encode monster counts */
    e += sprintf(e, "%d,", new->common->num_ghosts);
    e += sprintf(e, "%d,", new->common->num_vampires);
    e += sprintf(e, "%d,", new->common->num_zombies);

    /* Encode grid */
    count = 0;
    for (y=1;y<new->common->params.h+1;y++)
        for (x=1;x<new->common->params.w+1;x++) {
            c = new->common->grid[x+y*(new->common->params.w+2)];
            if (count > 25) {
                *e++ = 'z';
                count -= 26;
            }
            if (c != CELL_MIRROR_L && c != CELL_MIRROR_R) {
                count++;
            }
            else if (c == CELL_MIRROR_L) {
                if (count > 0) *e++ = (count-1 + 'a');
                *e++ = 'L';
                count = 0;
            }
            else {
                if (count > 0) *e++ = (count-1 + 'a');
                *e++ = 'R';
                count = 0;          
            }
        }
    if (count > 0) *e++ = (count-1 + 'a');

    /* Encode hints */
    for (p=0;p<2*(new->common->params.w + new->common->params.h);p++) {
        range2grid(p,new->common->params.w,new->common->params.h,&x,&y);
        e += sprintf(e, ",%d", new->common->grid[x+y*(new->common->params.w+2)]);
    }

    *e++ = '\0';
    desc = sresize(desc, e - desc, char);

    sfree(old_guess);
    free_game(new);

    return desc;
}

static void num2grid(int num, int width, int height, int *x, int *y) {
    *x = 1+(num%width);
    *y = 1+(num/width);
    return;
}

static key_label *game_request_keys(const game_params *params, int *nkeys)
{
    key_label *keys = snewn(4, key_label);
    *nkeys = 4;

    keys[0].button = 'G';
    keys[0].label = dupstr("Ghost");

    keys[1].button = 'V';
    keys[1].label = dupstr("Vampire");

    keys[2].button = 'Z';
    keys[2].label = dupstr("Zombie");

    keys[3].button = '\b';
    keys[3].label = NULL;

    return keys;
}

static game_state *new_game(midend *me, const game_params *params,
                            const char *desc)
{
    int i;
    int n;
    int count;

    game_state *state = new_state(params);

    state->common->num_ghosts = atoi(desc);
    while (*desc && isdigit((unsigned char)*desc)) desc++;
    desc++;
    state->common->num_vampires = atoi(desc);
    while (*desc && isdigit((unsigned char)*desc)) desc++;
    desc++;
    state->common->num_zombies = atoi(desc);
    while (*desc && isdigit((unsigned char)*desc)) desc++;
    desc++;

    state->common->num_total = state->common->num_ghosts + state->common->num_vampires + state->common->num_zombies;

    state->guess = snewn(state->common->num_total,int);
    state->pencils = snewn(state->common->num_total,unsigned char);
    state->common->fixed = snewn(state->common->num_total, bool);
    for (i=0;i<state->common->num_total;i++) {
        state->guess[i] = 7;
        state->pencils[i] = 0;
        state->common->fixed[i] = false;
    }
    for (i=0;i<state->common->wh;i++)
        state->cell_errors[i] = false;
    for (i=0;i<2*state->common->num_paths;i++)
        state->hint_errors[i] = false;
    for (i=0;i<3;i++)
        state->count_errors[i] = false;

    count = 0;
    n = 0;
    while (*desc != ',') {
        int c;
        int x,y;

        if (*desc == 'L') {
            num2grid(n,state->common->params.w,state->common->params.h,&x,&y);
            state->common->grid[x+y*(state->common->params.w +2)] = CELL_MIRROR_L;
            state->common->xinfo[x+y*(state->common->params.w+2)] = -1;
            n++;
        }
        else if (*desc == 'R') {
            num2grid(n,state->common->params.w,state->common->params.h,&x,&y);
            state->common->grid[x+y*(state->common->params.w +2)] = CELL_MIRROR_R;
            state->common->xinfo[x+y*(state->common->params.w+2)] = -1;
            n++;
        }
        else if (*desc == 'G') {
            num2grid(n,state->common->params.w,state->common->params.h,&x,&y);
            state->common->grid[x+y*(state->common->params.w +2)] = CELL_GHOST;
            state->common->xinfo[x+y*(state->common->params.w+2)] = count;
            state->guess[count] = 1;
            state->common->fixed[count++] = true;
            n++;
        }
        else if (*desc == 'V') {
            num2grid(n,state->common->params.w,state->common->params.h,&x,&y);
            state->common->grid[x+y*(state->common->params.w +2)] = CELL_VAMPIRE;
            state->common->xinfo[x+y*(state->common->params.w+2)] = count;
            state->guess[count] = 2;
            state->common->fixed[count++] = true;
            n++;
        }
        else if (*desc == 'Z') {
            num2grid(n,state->common->params.w,state->common->params.h,&x,&y);
            state->common->grid[x+y*(state->common->params.w +2)] = CELL_ZOMBIE;
            state->common->xinfo[x+y*(state->common->params.w+2)] = count;
            state->guess[count] = 4;
            state->common->fixed[count++] = true;
            n++;
        }
        else {
            c = *desc - ('a' -1);
            while (c-- > 0) {
                num2grid(n,state->common->params.w,state->common->params.h,&x,&y);
                state->common->grid[x+y*(state->common->params.w +2)] = CELL_EMPTY;
                state->common->xinfo[x+y*(state->common->params.w+2)] = count;
                state->guess[count] = 7;
                state->common->fixed[count++] = false;
                n++;
            }
        }
        desc++;
    }
    desc++;

    for (i=0;i<2*(state->common->params.w + state->common->params.h);i++) {
        int x,y;
        int sights;

        sights = atoi(desc);
        while (*desc && isdigit((unsigned char)*desc)) desc++;
        desc++;


        range2grid(i,state->common->params.w,state->common->params.h,&x,&y);
        state->common->grid[x+y*(state->common->params.w +2)] = sights;
        state->common->xinfo[x+y*(state->common->params.w +2)] = -2;
    }

    state->common->grid[0] = 0;
    state->common->xinfo[0] = -2;
    state->common->grid[state->common->params.w+1] = 0;
    state->common->xinfo[state->common->params.w+1] = -2;
    state->common->grid[state->common->params.w+1 + (state->common->params.h+1)*(state->common->params.w+2)] = 0;
    state->common->xinfo[state->common->params.w+1 + (state->common->params.h+1)*(state->common->params.w+2)] = -2;
    state->common->grid[(state->common->params.h+1)*(state->common->params.w+2)] = 0;
    state->common->xinfo[(state->common->params.h+1)*(state->common->params.w+2)] = -2;

    make_paths(state);
    qsort(state->common->paths, state->common->num_paths, sizeof(struct path), path_cmp);

    return state;
}

static const char *validate_desc(const game_params *params, const char *desc)
{
    int i;
    int w = params->w, h = params->h;
    int wh = w*h;
    int area;
    int monsters;
    int monster_count;
    const char *desc_s = desc;
        
    for (i=0;i<3;i++) {
        if (!*desc) return "Faulty game description";
        while (*desc && isdigit((unsigned char)*desc)) { desc++; }
        if (*desc != ',') return "Invalid character in number list";
        desc++;
    }
    desc = desc_s;
    
    area = monsters = monster_count = 0;
    for (i=0;i<3;i++) {
        monster_count += atoi(desc);
        while (*desc && isdigit((unsigned char)*desc)) desc++;
        desc++;
    }
    while (*desc && *desc != ',') {
        if (*desc >= 'a' && *desc <= 'z') {
            area += *desc - 'a' +1; monsters += *desc - 'a' +1;
        } else if (*desc == 'G' || *desc == 'V' || *desc == 'Z') {
            area++; monsters++;
        } else if (*desc == 'L' || *desc == 'R') {
            area++;
        } else
            return "Invalid character in grid specification";
        desc++;
    }
    if (area < wh) return "Not enough data to fill grid";
    else if (area > wh) return "Too much data to fill grid";
    if (monsters != monster_count)
        return "Monster numbers do not match grid spaces";

    for (i = 0; i < 2*(w+h); i++) {
        if (!*desc) return "Not enough numbers given after grid specification";
        else if (*desc != ',') return "Invalid character in number list";
        desc++;
        while (*desc && isdigit((unsigned char)*desc)) { desc++; }
    }

    if (*desc) return "Unexpected additional data at end of game description";

    return NULL;
}

static char *solve_game(const game_state *state_start, const game_state *currstate,
                        const char *aux, const char **error)
{
    int p;
    int *old_guess;
    int iterative_depth;
    bool solved_iterative, solved_bruteforce, contains_inconsistency;
    int count_ambiguous;

    int i;
    char *move, *c;

    game_state *solve_state = dup_game(currstate);

    old_guess = snewn(solve_state->common->num_total,int);
    for (p=0;p<solve_state->common->num_total;p++) {
        if (solve_state->common->fixed[p]) {
            old_guess[p] = solve_state->guess[p] = state_start->guess[p];
        }
        else {
            old_guess[p] = solve_state->guess[p] = 7;
        }
    }
    iterative_depth = 0;
    solved_iterative = false;
    contains_inconsistency = false;
    count_ambiguous = 0;
    
    /* Try to solve the puzzle with the iterative solver */
    while (true) {
        bool no_change = true;
        solved_iterative =
            solve_iterative(solve_state,solve_state->common->paths);
        iterative_depth++;
        for (p=0;p<solve_state->common->num_total;p++) {
            if (solve_state->guess[p] != old_guess[p]) no_change = false;
            old_guess[p] = solve_state->guess[p];
            if (solve_state->guess[p] == 0) contains_inconsistency = true;
        }
        if (solved_iterative || no_change || contains_inconsistency) break;
    }

    if (contains_inconsistency) {
        *error = "Puzzle is inconsistent";
        sfree(old_guess);
        free_game(solve_state);
        return NULL;
    }

    /* If necessary, try to solve the puzzle with the brute-force solver */
    solved_bruteforce = false;  
    if (!solved_iterative) {
        for (p=0;p<solve_state->common->num_total;p++)
            if (solve_state->guess[p] != 1 && solve_state->guess[p] != 2 &&
                solve_state->guess[p] != 4) count_ambiguous++;
        solved_bruteforce =
            solve_bruteforce(solve_state, solve_state->common->paths);
    }

    if (!solved_iterative && !solved_bruteforce) {
        *error = "Puzzle is unsolvable";
        sfree(old_guess);
        free_game(solve_state);
        return NULL;
    }

/*  printf("Puzzle solved at level %s, iterations %d, ambiguous %d\n", (solved_bruteforce ? "TRICKY" : "NORMAL"), iterative_depth, count_ambiguous); */

    move = snewn(solve_state->common->num_total * 4 +2, char);
    c = move;
    *c++='S';
    for (i = 0; i < solve_state->common->num_total; i++) {
        if (solve_state->guess[i] == 1) c += sprintf(c, ";G%d", i);
        if (solve_state->guess[i] == 2) c += sprintf(c, ";V%d", i);
        if (solve_state->guess[i] == 4) c += sprintf(c, ";Z%d", i);
    }
    *c++ = '\0';
    move = sresize(move, c - move, char);

    sfree(old_guess);   
    free_game(solve_state);
    return move;
}

static bool game_can_format_as_text_now(const game_params *params)
{
    return true;
}

static char *game_text_format(const game_state *state)
{
    int w,h,c,r,xi,g;
    char *ret;
    char buf[120];

    ret = snewn(50 + 6*(state->common->params.w +2) +
                6*(state->common->params.h+2) +
                3*(state->common->params.w * state->common->params.h), char);

    sprintf(ret,"G: %d V: %d Z: %d\n\n",state->common->num_ghosts,
            state->common->num_vampires, state->common->num_zombies);

    for (h=0;h<state->common->params.h+2;h++) {
        for (w=0;w<state->common->params.w+2;w++) {
            c = state->common->grid[w+h*(state->common->params.w+2)];
            xi = state->common->xinfo[w+h*(state->common->params.w+2)];
            r = grid2range(w,h,state->common->params.w,state->common->params.h);
            if (r != -1) {
                sprintf(buf,"%2d", c);  strcat(ret,buf);
            } else if (c == CELL_MIRROR_L) {
                sprintf(buf," \\"); strcat(ret,buf);
            } else if (c == CELL_MIRROR_R) {
                sprintf(buf," /"); strcat(ret,buf);
            } else if (xi >= 0) {
                g = state->guess[xi];
                if (g == 1)      { sprintf(buf," G"); strcat(ret,buf); }
                else if (g == 2) { sprintf(buf," V"); strcat(ret,buf); }
                else if (g == 4) { sprintf(buf," Z"); strcat(ret,buf); }
                else             { sprintf(buf," ."); strcat(ret,buf); }
            } else {
                sprintf(buf,"  "); strcat(ret,buf);
            }
        }
        sprintf(buf,"\n"); strcat(ret,buf);
    }

    return ret;
}

struct game_ui {
    int hx, hy;                         /* as for solo.c, highlight pos */
    bool hshow, hpencil, hcursor;       /* show state, type, and ?cursor. */
    bool ascii;
};

static game_ui *new_ui(const game_state *state)
{
    game_ui *ui = snew(game_ui);
    ui->hpencil = false;
    ui->hx = ui->hy = ui->hshow = ui->hcursor =
        getenv_bool("PUZZLES_SHOW_CURSOR", false);
    ui->ascii = false;
    return ui;
}

static void free_ui(game_ui *ui) {
    sfree(ui);
    return;
}

static char *encode_ui(const game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, const char *encoding)
{
    return;
}

static void game_changed_state(game_ui *ui, const game_state *oldstate,
                               const game_state *newstate)
{
    /* See solo.c; if we were pencil-mode highlighting and
     * somehow a square has just been properly filled, cancel
     * pencil mode. */
    if (ui->hshow && ui->hpencil && !ui->hcursor) {
        int g = newstate->guess[newstate->common->xinfo[ui->hx + ui->hy*(newstate->common->params.w+2)]];
        if (g == 1 || g == 2 || g == 4)
            ui->hshow = false;
    }
}

static const char *current_key_label(const game_ui *ui,
                                     const game_state *state, int button)
{
    int xi;

    if (ui->hshow && button == CURSOR_SELECT)
        return ui->hpencil ? "Ink" : "Pencil";
    if (button == CURSOR_SELECT2) {
        xi = state->common->xinfo[ui->hx + ui->hy*(state->common->params.w+2)];
        if (xi >= 0 && !state->common->fixed[xi]) return "Clear";
    }
    return "";
}

struct game_drawstate {
    int tilesize;
    bool started, solved;
    int w, h;
        
    int *monsters;
    unsigned char *pencils;

    bool count_errors[3];
    bool *cell_errors;
    bool *hint_errors;
    bool *hints_done;

    int hx, hy;
    bool hshow, hpencil; /* as for game_ui. */
    bool hflash;
    bool ascii;
};

static bool is_clue(const game_state *state, int x, int y)
{
    int h = state->common->params.h, w = state->common->params.w;

    if (((x == 0 || x == w + 1) && y > 0 && y <= h) ||
        ((y == 0 || y == h + 1) && x > 0 && x <= w))
        return true;

    return false;
}

static int clue_index(const game_state *state, int x, int y)
{
    int h = state->common->params.h, w = state->common->params.w;

    if (y == 0)
        return x - 1;
    else if (x == w + 1)
        return w + y - 1;
    else if (y == h + 1)
        return 2 * w + h - x;
    else if (x == 0)
        return 2 * (w + h) - y;

    return -1;
}

#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE/4)

static char *interpret_move(const game_state *state, game_ui *ui,
                            const game_drawstate *ds,
                            int x, int y, int button)
{
    int gx,gy;
    int g,xi;
    char buf[80]; 

    gx = ((x-BORDER-1) / TILESIZE );
    gy = ((y-BORDER-2) / TILESIZE ) - 1;

    if (button == 'a' || button == 'A') {
        ui->ascii = !ui->ascii;
        return UI_UPDATE;
    }

    if (button == 'm' || button == 'M') {
        return dupstr("M");
    }
    
    if (ui->hshow && !ui->hpencil) {
        xi = state->common->xinfo[ui->hx + ui->hy*(state->common->params.w+2)];
        if (xi >= 0 && !state->common->fixed[xi]) {
            if (button == 'g' || button == 'G' || button == '1') {
                if (!ui->hcursor) ui->hshow = false;
                if (state->guess[xi] == 1)
                    return ui->hcursor ? NULL : UI_UPDATE;
                sprintf(buf,"G%d",xi);
                return dupstr(buf);
            }
            if (button == 'v' || button == 'V' || button == '2') {
                if (!ui->hcursor) ui->hshow = false;
                if (state->guess[xi] == 2)
                    return ui->hcursor ? NULL : UI_UPDATE;
                sprintf(buf,"V%d",xi);
                return dupstr(buf);
            }
            if (button == 'z' || button == 'Z' || button == '3') {
                if (!ui->hcursor) ui->hshow = false;
                if (state->guess[xi] == 4)
                    return ui->hcursor ? NULL : UI_UPDATE;
                sprintf(buf,"Z%d",xi);
                return dupstr(buf);
            }
            if (button == 'e' || button == 'E' || button == CURSOR_SELECT2 ||
                button == '0' || button == '\b' ) {
                if (!ui->hcursor) ui->hshow = false;
                if (state->guess[xi] == 7 && state->pencils[xi] == 0)
                    return ui->hcursor ? NULL : UI_UPDATE;
                sprintf(buf,"E%d",xi);
                return dupstr(buf);
            }
        }       
    }

    if (IS_CURSOR_MOVE(button)) {
        if (ui->hx == 0 && ui->hy == 0) {
            ui->hx = 1;
            ui->hy = 1;
        }
        else switch (button) {
              case CURSOR_UP:     ui->hy -= (ui->hy > 1)     ? 1 : 0; break;
              case CURSOR_DOWN:   ui->hy += (ui->hy < ds->h) ? 1 : 0; break;
              case CURSOR_RIGHT:  ui->hx += (ui->hx < ds->w) ? 1 : 0; break;
              case CURSOR_LEFT:   ui->hx -= (ui->hx > 1)     ? 1 : 0; break;
            }
        ui->hshow = true;
        ui->hcursor = true;
        return UI_UPDATE;
    }
    if (ui->hshow && button == CURSOR_SELECT) {
        ui->hpencil = !ui->hpencil;
        ui->hcursor = true;
        return UI_UPDATE;
    }

    if (ui->hshow && ui->hpencil) {
        xi = state->common->xinfo[ui->hx + ui->hy*(state->common->params.w+2)];
        if (xi >= 0 && !state->common->fixed[xi]) {
            if (button == 'g' || button == 'G' || button == '1') {
                sprintf(buf,"g%d",xi);
                if (!ui->hcursor) {
                    ui->hpencil = false;
                    ui->hshow = false;
                }
                return dupstr(buf);
            }
            if (button == 'v' || button == 'V' || button == '2') {
                sprintf(buf,"v%d",xi);
                if (!ui->hcursor) {
                    ui->hpencil = false;
                    ui->hshow = false;
                }
                return dupstr(buf);
            }
            if (button == 'z' || button == 'Z' || button == '3') {
                sprintf(buf,"z%d",xi);
                if (!ui->hcursor) {
                    ui->hpencil = false;
                    ui->hshow = false;
                }
                return dupstr(buf);
            }
            if (button == 'e' || button == 'E' || button == CURSOR_SELECT2 ||
                button == '0' || button == '\b') {
                if (!ui->hcursor) {
                    ui->hpencil = false;
                    ui->hshow = false;
                }
                if (state->pencils[xi] == 0)
                    return ui->hcursor ? NULL : UI_UPDATE;
                sprintf(buf,"E%d",xi);
                return dupstr(buf);
            }
        }       
    }

    if (gx > 0 && gx < ds->w+1 && gy > 0 && gy < ds->h+1) {
        xi = state->common->xinfo[gx+gy*(state->common->params.w+2)];
        if (xi >= 0 && !state->common->fixed[xi]) {
            g = state->guess[xi];
            if (!ui->hshow) {
                if (button == LEFT_BUTTON) {
                    ui->hshow = true;
                    ui->hpencil = false;
                    ui->hcursor = false;
                    ui->hx = gx; ui->hy = gy;
                    return UI_UPDATE;
                }
                else if (button == RIGHT_BUTTON && g == 7) {
                    ui->hshow = true;
                    ui->hpencil = true;
                    ui->hcursor = false;
                    ui->hx = gx; ui->hy = gy;
                    return UI_UPDATE;
                }
            }
            else if (ui->hshow) {
                if (button == LEFT_BUTTON) {
                    if (!ui->hpencil) {
                        if (gx == ui->hx && gy == ui->hy) {
                            ui->hshow = false;
                            ui->hpencil = false;
                            ui->hcursor = false;
                            ui->hx = 0; ui->hy = 0;
                            return UI_UPDATE;
                        }
                        else {
                            ui->hshow = true;
                            ui->hpencil = false;
                            ui->hcursor = false;
                            ui->hx = gx; ui->hy = gy;
                            return UI_UPDATE;
                        }
                    }
                    else {
                        ui->hshow = true;
                        ui->hpencil = false;
                        ui->hcursor = false;
                        ui->hx = gx; ui->hy = gy;
                        return UI_UPDATE;
                    }
                }
                else if (button == RIGHT_BUTTON) {
                    if (!ui->hpencil && g == 7) {
                        ui->hshow = true;
                        ui->hpencil = true;
                        ui->hcursor = false;
                        ui->hx = gx; ui->hy = gy;
                        return UI_UPDATE;
                    }
                    else {
                        if (gx == ui->hx && gy == ui->hy) {
                            ui->hshow = false;
                            ui->hpencil = false;
                            ui->hcursor = false;
                            ui->hx = 0; ui->hy = 0;
                            return UI_UPDATE;
                        }
                        else if (g == 7) {
                            ui->hshow = true;
                            ui->hpencil = true;
                            ui->hcursor = false;
                            ui->hx = gx; ui->hy = gy;
                            return UI_UPDATE;
                        }
                    }
                }
            }
        }
    } else if (button == LEFT_BUTTON) {
        if (is_clue(state, gx, gy)) {
            sprintf(buf, "D%d,%d", gx, gy);
            return dupstr(buf);
        }
    }

    return NULL;
}

static bool check_numbers_draw(game_state *state, int *guess) {
    bool valid, filled;
    int i,x,y,xy;
    int count_ghosts, count_vampires, count_zombies;

    count_ghosts = count_vampires = count_zombies = 0;  
    for (i=0;i<state->common->num_total;i++) {
        if (guess[i] == 1) count_ghosts++;
        if (guess[i] == 2) count_vampires++;
        if (guess[i] == 4) count_zombies++;
    }

    valid = true;
    filled = (count_ghosts + count_vampires + count_zombies >=
              state->common->num_total);

    if (count_ghosts > state->common->num_ghosts ||
        (filled && count_ghosts != state->common->num_ghosts) ) {
        valid = false; 
        state->count_errors[0] = true; 
        for (x=1;x<state->common->params.w+1;x++)
            for (y=1;y<state->common->params.h+1;y++) {
                xy = x+y*(state->common->params.w+2);
                if (state->common->xinfo[xy] >= 0 &&
                    guess[state->common->xinfo[xy]] == 1)
                    state->cell_errors[xy] = true;
            }
    }
    if (count_vampires > state->common->num_vampires ||
        (filled && count_vampires != state->common->num_vampires) ) {
        valid = false; 
        state->count_errors[1] = true; 
        for (x=1;x<state->common->params.w+1;x++)
            for (y=1;y<state->common->params.h+1;y++) {
                xy = x+y*(state->common->params.w+2);
                if (state->common->xinfo[xy] >= 0 &&
                    guess[state->common->xinfo[xy]] == 2)
                    state->cell_errors[xy] = true;
            }
    }
    if (count_zombies > state->common->num_zombies ||
        (filled && count_zombies != state->common->num_zombies) )  {
        valid = false; 
        state->count_errors[2] = true; 
        for (x=1;x<state->common->params.w+1;x++)
            for (y=1;y<state->common->params.h+1;y++) {
                xy = x+y*(state->common->params.w+2);
                if (state->common->xinfo[xy] >= 0 &&
                    guess[state->common->xinfo[xy]] == 4)
                    state->cell_errors[xy] = true;
            }
    }

    return valid;
}

static bool check_path_solution(game_state *state, int p) {
    int i;
    bool mirror;
    int count;
    bool correct;
    int unfilled;

    count = 0;
    mirror = false;
    correct = true;

    unfilled = 0;
    for (i=0;i<state->common->paths[p].length;i++) {
        if (state->common->paths[p].p[i] == -1) mirror = true;
        else {
            if (state->guess[state->common->paths[p].p[i]] == 1 && mirror)
                count++;
            else if (state->guess[state->common->paths[p].p[i]] == 2 && !mirror)
                count++;
            else if (state->guess[state->common->paths[p].p[i]] == 4)
                count++;
            else if (state->guess[state->common->paths[p].p[i]] == 7)
                unfilled++;
        }
    }

    if (count            > state->common->paths[p].sightings_start ||
        count + unfilled < state->common->paths[p].sightings_start)
    {
        correct = false;
        state->hint_errors[state->common->paths[p].grid_start] = true;
    }

    count = 0;
    mirror = false;
    unfilled = 0;
    for (i=state->common->paths[p].length-1;i>=0;i--) {
        if (state->common->paths[p].p[i] == -1) mirror = true;
        else {
            if (state->guess[state->common->paths[p].p[i]] == 1 && mirror)
                count++;
            else if (state->guess[state->common->paths[p].p[i]] == 2 && !mirror)
                count++;
            else if (state->guess[state->common->paths[p].p[i]] == 4)
                count++;
            else if (state->guess[state->common->paths[p].p[i]] == 7)
                unfilled++;
        }
    }

    if (count            > state->common->paths[p].sightings_end ||
        count + unfilled < state->common->paths[p].sightings_end)
    {
        correct = false;
        state->hint_errors[state->common->paths[p].grid_end] = true;
    }

    if (!correct) {
        for (i=0;i<state->common->paths[p].length;i++) 
            state->cell_errors[state->common->paths[p].xy[i]] = true;
    }

    return correct;
}

static game_state *execute_move(const game_state *state, const char *move)
{
    int x,y,n,p,i;
    char c;
    bool correct; 
    bool solver; 

    game_state *ret = dup_game(state);
    solver = false;

    while (*move) {
        c = *move;
        if (c == 'S') {
            move++;
            solver = true;
        } else if (c == 'G' || c == 'V' || c == 'Z' || c == 'E' ||
                   c == 'g' || c == 'v' || c == 'z') {
            move++;
            if (sscanf(move, "%d%n", &x, &n) != 1) goto badmove;
            if (x < 0 || x >= ret->common->num_total) goto badmove;
            if (c == 'G') ret->guess[x] = 1;
            if (c == 'V') ret->guess[x] = 2;
            if (c == 'Z') ret->guess[x] = 4;
            if (c == 'E') { ret->guess[x] = 7; ret->pencils[x] = 0; }
            if (c == 'g') ret->pencils[x] ^= 1;
            if (c == 'v') ret->pencils[x] ^= 2;
            if (c == 'z') ret->pencils[x] ^= 4;
            move += n;
        } else if (c == 'D' && sscanf(move + 1, "%d,%d%n", &x, &y, &n) == 2 &&
                   is_clue(ret, x, y)) {
            ret->hints_done[clue_index(ret, x, y)] ^= 1;
            move += n + 1;
        } else if (c == 'M') {
            /*
             * Fill in absolutely all pencil marks in unfilled
             * squares, for those who like to play by the rigorous
             * approach of starting off in that state and eliminating
             * things.
             */
            for (i = 0; i < ret->common->num_total; i++)
                if (ret->guess[i] == 7)
                    ret->pencils[i] = 7;
            move++;
        } else {
            /* Unknown move type. */
        badmove:
            free_game(ret);
            return NULL;
        }
        if (*move == ';') move++;
    }

    correct = true;

    for (i=0;i<ret->common->wh;i++) ret->cell_errors[i] = false;
    for (i=0;i<2*ret->common->num_paths;i++) ret->hint_errors[i] = false;
    for (i=0;i<3;i++) ret->count_errors[i] = false;

    if (!check_numbers_draw(ret,ret->guess)) correct = false;

    for (p=0;p<state->common->num_paths;p++)
        if (!check_path_solution(ret,p)) correct = false;

    for (i=0;i<state->common->num_total;i++)
        if (!(ret->guess[i] == 1 || ret->guess[i] == 2 ||
              ret->guess[i] == 4)) correct = false;

    if (correct && !solver) ret->solved = true;
    if (solver) ret->cheated = true;

    return ret;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define PREFERRED_TILE_SIZE 64

static void game_compute_size(const game_params *params, int tilesize,
                              int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = 2*BORDER+(params->w+2)*TILESIZE;
    *y = 2*BORDER+(params->h+3)*TILESIZE;
    return;
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          const game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
    return;
}

#define COLOUR(ret, i, r, g, b)     ((ret[3*(i)+0] = (r)), (ret[3*(i)+1] = (g)), (ret[3*(i)+2] = (b)))

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_TEXT * 3 + 0] = 0.0F;
    ret[COL_TEXT * 3 + 1] = 0.0F;
    ret[COL_TEXT * 3 + 2] = 0.0F;

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.78F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.78F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.78F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_FLASH * 3 + 0] = 1.0F;
    ret[COL_FLASH * 3 + 1] = 1.0F;
    ret[COL_FLASH * 3 + 2] = 1.0F;

    ret[COL_GHOST * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 0.5F;
    ret[COL_GHOST * 3 + 1] = ret[COL_BACKGROUND * 3 + 0];
    ret[COL_GHOST * 3 + 2] = ret[COL_BACKGROUND * 3 + 0];

    ret[COL_ZOMBIE * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] * 0.5F;
    ret[COL_ZOMBIE * 3 + 1] = ret[COL_BACKGROUND * 3 + 0];
    ret[COL_ZOMBIE * 3 + 2] = ret[COL_BACKGROUND * 3 + 0] * 0.5F;

    ret[COL_VAMPIRE * 3 + 0] = ret[COL_BACKGROUND * 3 + 0];
    ret[COL_VAMPIRE * 3 + 1] = ret[COL_BACKGROUND * 3 + 0] * 0.9F;
    ret[COL_VAMPIRE * 3 + 2] = ret[COL_BACKGROUND * 3 + 0] * 0.9F;

    ret[COL_DONE * 3 + 0] = ret[COL_BACKGROUND * 3 + 0] / 1.5F;
    ret[COL_DONE * 3 + 1] = ret[COL_BACKGROUND * 3 + 1] / 1.5F;
    ret[COL_DONE * 3 + 2] = ret[COL_BACKGROUND * 3 + 2] / 1.5F;

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, const game_state *state)
{
    int i;
    struct game_drawstate *ds = snew(struct game_drawstate);

    ds->tilesize = 0;
    ds->started = ds->solved = false;
    ds->w = state->common->params.w;
    ds->h = state->common->params.h;
    ds->ascii = false;
    
    ds->count_errors[0] = false;
    ds->count_errors[1] = false;
    ds->count_errors[2] = false;

    ds->monsters = snewn(state->common->num_total,int);
    for (i=0;i<(state->common->num_total);i++)
        ds->monsters[i] = 7;
    ds->pencils = snewn(state->common->num_total,unsigned char);
    for (i=0;i<state->common->num_total;i++)
        ds->pencils[i] = 0;

    ds->cell_errors = snewn(state->common->wh,bool);
    for (i=0;i<state->common->wh;i++)
        ds->cell_errors[i] = false;
    ds->hint_errors = snewn(2*state->common->num_paths,bool);
    for (i=0;i<2*state->common->num_paths;i++)
        ds->hint_errors[i] = false;
    ds->hints_done = snewn(2 * state->common->num_paths, bool);
    memset(ds->hints_done, 0,
           2 * state->common->num_paths * sizeof(bool));

    ds->hshow = false;
    ds->hpencil = false;
    ds->hflash = false;
    ds->hx = ds->hy = 0;
    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds) {
    sfree(ds->hints_done);
    sfree(ds->hint_errors);
    sfree(ds->cell_errors);
    sfree(ds->pencils);
    sfree(ds->monsters);
    sfree(ds);
    return;
}

static void draw_cell_background(drawing *dr, game_drawstate *ds,
                                 const game_state *state, const game_ui *ui,
                                 int x, int y) {

    bool hon;
    int dx,dy;
    dx = BORDER+(x* ds->tilesize)+(TILESIZE/2);
    dy = BORDER+(y* ds->tilesize)+(TILESIZE/2)+TILESIZE;

    hon = (ui->hshow && x == ui->hx && y == ui->hy);
    draw_rect(dr,dx-(TILESIZE/2)+1,dy-(TILESIZE/2)+1,TILESIZE-1,TILESIZE-1,(hon && !ui->hpencil) ? COL_HIGHLIGHT : COL_BACKGROUND);

    if (hon && ui->hpencil) {
        int coords[6];
        coords[0] = dx-(TILESIZE/2)+1;
        coords[1] = dy-(TILESIZE/2)+1;
        coords[2] = coords[0] + TILESIZE/2;
        coords[3] = coords[1];
        coords[4] = coords[0];
        coords[5] = coords[1] + TILESIZE/2;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    }

    draw_update(dr,dx-(TILESIZE/2)+1,dy-(TILESIZE/2)+1,TILESIZE-1,TILESIZE-1);

    return;
}

static void draw_circle_or_point(drawing *dr, int cx, int cy, int radius,
                                 int colour)
{
    if (radius > 0)
        draw_circle(dr, cx, cy, radius, colour, colour);
    else
        draw_rect(dr, cx, cy, 1, 1, colour);
}

static void draw_monster(drawing *dr, game_drawstate *ds, int x, int y,
                         int tilesize, bool hflash, int monster)
{
    int black = (hflash ? COL_FLASH : COL_TEXT);
    
    if (monster == 1) {                /* ghost */
        int poly[80], i, j;

        clip(dr,x-(tilesize/2)+2,y-(tilesize/2)+2,tilesize-3,tilesize/2+1);
        draw_circle(dr,x,y,2*tilesize/5, COL_GHOST,black);
        unclip(dr);

        i = 0;
        poly[i++] = x - 2*tilesize/5;
        poly[i++] = y-2;
        poly[i++] = x - 2*tilesize/5;
        poly[i++] = y + 2*tilesize/5;

        for (j = 0; j < 3; j++) {
            int total = (2*tilesize/5) * 2;
            int before = total * j / 3;
            int after = total * (j+1) / 3;
            int mid = (before + after) / 2;
            poly[i++] = x - 2*tilesize/5 + mid;
            poly[i++] = y + 2*tilesize/5 - (total / 6);
            poly[i++] = x - 2*tilesize/5 + after;
            poly[i++] = y + 2*tilesize/5;
        }

        poly[i++] = x + 2*tilesize/5;
        poly[i++] = y-2;

        clip(dr,x-(tilesize/2)+2,y,tilesize-3,tilesize-(tilesize/2)-1);
        draw_polygon(dr, poly, i/2, COL_GHOST, black);
        unclip(dr);

        draw_circle(dr,x-tilesize/6,y-tilesize/12,tilesize/10,
                    COL_BACKGROUND,black);
        draw_circle(dr,x+tilesize/6,y-tilesize/12,tilesize/10,
                    COL_BACKGROUND,black);
        
        draw_circle_or_point(dr,x-tilesize/6+1+tilesize/48,y-tilesize/12,
                             tilesize/48,black);
        draw_circle_or_point(dr,x+tilesize/6+1+tilesize/48,y-tilesize/12,
                             tilesize/48,black);
        
    } else if (monster == 2) {         /* vampire */
        int poly[80], i;

        clip(dr,x-(tilesize/2)+2,y-(tilesize/2)+2,tilesize-3,tilesize/2);
        draw_circle(dr,x,y,2*tilesize/5,black,black);
        unclip(dr);

        clip(dr,x-(tilesize/2)+2,y-(tilesize/2)+2,tilesize/2+1,tilesize/2);
        draw_circle(dr,x-tilesize/7,y,2*tilesize/5-tilesize/7,
                    COL_VAMPIRE,black);
        unclip(dr);
        clip(dr,x,y-(tilesize/2)+2,tilesize/2+1,tilesize/2);
        draw_circle(dr,x+tilesize/7,y,2*tilesize/5-tilesize/7,
                    COL_VAMPIRE,black);
        unclip(dr);

        clip(dr,x-(tilesize/2)+2,y,tilesize-3,tilesize/2);
        draw_circle(dr,x,y,2*tilesize/5, COL_VAMPIRE,black);
        unclip(dr);

        draw_circle(dr, x-tilesize/7, y-tilesize/16, tilesize/16,
                    COL_BACKGROUND, black);
        draw_circle(dr, x+tilesize/7, y-tilesize/16, tilesize/16,
                    COL_BACKGROUND, black);
        draw_circle_or_point(dr, x-tilesize/7, y-tilesize/16, tilesize/48,
                             black);
        draw_circle_or_point(dr, x+tilesize/7, y-tilesize/16, tilesize/48,
                             black);

        clip(dr, x-(tilesize/2)+2, y+tilesize/8, tilesize-3, tilesize/4);

        i = 0;
        poly[i++] = x-3*tilesize/16;
        poly[i++] = y+1*tilesize/8;
        poly[i++] = x-2*tilesize/16;
        poly[i++] = y+7*tilesize/24;
        poly[i++] = x-1*tilesize/16;
        poly[i++] = y+1*tilesize/8;
        draw_polygon(dr, poly, i/2, COL_BACKGROUND, black);
        i = 0;
        poly[i++] = x+3*tilesize/16;
        poly[i++] = y+1*tilesize/8;
        poly[i++] = x+2*tilesize/16;
        poly[i++] = y+7*tilesize/24;
        poly[i++] = x+1*tilesize/16;
        poly[i++] = y+1*tilesize/8;
        draw_polygon(dr, poly, i/2, COL_BACKGROUND, black);

        draw_circle(dr, x, y-tilesize/5, 2*tilesize/5, COL_VAMPIRE, black);
        unclip(dr);

    } else if (monster == 4) {         /* zombie */
        draw_circle(dr,x,y,2*tilesize/5, COL_ZOMBIE,black);

        draw_line(dr,
                  x-tilesize/7-tilesize/16, y-tilesize/12-tilesize/16,
                  x-tilesize/7+tilesize/16, y-tilesize/12+tilesize/16,
                  black);
        draw_line(dr,
                  x-tilesize/7+tilesize/16, y-tilesize/12-tilesize/16,
                  x-tilesize/7-tilesize/16, y-tilesize/12+tilesize/16,
                  black);
        draw_line(dr,
                  x+tilesize/7-tilesize/16, y-tilesize/12-tilesize/16,
                  x+tilesize/7+tilesize/16, y-tilesize/12+tilesize/16,
                  black);
        draw_line(dr,
                  x+tilesize/7+tilesize/16, y-tilesize/12-tilesize/16,
                  x+tilesize/7-tilesize/16, y-tilesize/12+tilesize/16,
                  black);

        clip(dr, x-tilesize/5, y+tilesize/6, 2*tilesize/5+1, tilesize/2);
        draw_circle(dr, x-tilesize/15, y+tilesize/6, tilesize/12,
                    COL_BACKGROUND, black);
        unclip(dr);

        draw_line(dr, x-tilesize/5, y+tilesize/6, x+tilesize/5, y+tilesize/6,
                  black);
    }

    draw_update(dr,x-(tilesize/2)+2,y-(tilesize/2)+2,tilesize-3,tilesize-3);
}

static void draw_monster_count(drawing *dr, game_drawstate *ds,
                               const game_state *state, int c, bool hflash) {
    int dx,dy;
    char buf[MAX_DIGITS(int) + 1];
    char bufm[8];
    
    dy = TILESIZE/4;
    dx = BORDER+(ds->w+2)*TILESIZE/2+TILESIZE/4;
    switch (c) {
      case 0: 
        sprintf(buf,"%d",state->common->num_ghosts);
        sprintf(bufm,"G");
        dx -= 3*TILESIZE/2;
        break;
      case 1: 
        sprintf(buf,"%d",state->common->num_vampires); 
        sprintf(bufm,"V");
        break;
      case 2: 
        sprintf(buf,"%d",state->common->num_zombies); 
        sprintf(bufm,"Z");
        dx += 3*TILESIZE/2;
        break;
    }

    draw_rect(dr, dx-2*TILESIZE/3, dy, 3*TILESIZE/2, TILESIZE,
              COL_BACKGROUND);
    if (!ds->ascii) { 
        draw_monster(dr, ds, dx-TILESIZE/3, dy+TILESIZE/2,
                     2*TILESIZE/3, hflash, 1<<c);
    } else {
        draw_text(dr, dx-TILESIZE/3,dy+TILESIZE/2,FONT_VARIABLE,TILESIZE/2,
                  ALIGN_HCENTRE|ALIGN_VCENTRE,
                  hflash ? COL_FLASH : COL_TEXT, bufm);
    }
    draw_text(dr, dx, dy+TILESIZE/2, FONT_VARIABLE, TILESIZE/2,
              ALIGN_HLEFT|ALIGN_VCENTRE,
              (state->count_errors[c] ? COL_ERROR :
               hflash ? COL_FLASH : COL_TEXT), buf);
    draw_update(dr, dx-2*TILESIZE/3, dy, 3*TILESIZE/2, TILESIZE);

    return;
}

static void draw_path_hint(drawing *dr, game_drawstate *ds,
                           const struct game_params *params,
                           int hint_index, bool hflash, int hint) {
    int x, y, color, dx, dy, text_dx, text_dy, text_size;
    char buf[MAX_DIGITS(int) + 1];

    if (ds->hint_errors[hint_index])
        color = COL_ERROR;
    else if (hflash)
        color = COL_FLASH;
    else if (ds->hints_done[hint_index])
        color = COL_DONE;
    else
        color = COL_TEXT;

    range2grid(hint_index, params->w, params->h, &x, &y);
    /* Upper-left corner of the "tile" */
    dx = BORDER + x * TILESIZE;
    dy = BORDER + y * TILESIZE + TILESIZE;
    /* Center of the "tile" */
    text_dx = dx + TILESIZE / 2;
    text_dy = dy +  TILESIZE / 2;
    /* Avoid wiping out the borders of the puzzle */
    dx += 2;
    dy += 2;
    text_size = TILESIZE - 3;

    sprintf(buf,"%d", hint);
    draw_rect(dr, dx, dy, text_size, text_size, COL_BACKGROUND);
    draw_text(dr, text_dx, text_dy, FONT_FIXED, TILESIZE / 2,
              ALIGN_HCENTRE | ALIGN_VCENTRE, color, buf);
    draw_update(dr, dx, dy, text_size, text_size);

    return;
}

static void draw_mirror(drawing *dr, game_drawstate *ds,
                        const game_state *state, int x, int y,
                        bool hflash, int mirror) {
    int dx,dy,mx1,my1,mx2,my2;
    dx = BORDER+(x* ds->tilesize)+(TILESIZE/2);
    dy = BORDER+(y* ds->tilesize)+(TILESIZE/2)+TILESIZE;

    if (mirror == CELL_MIRROR_L) {
        mx1 = dx-(TILESIZE/4);
        my1 = dy-(TILESIZE/4);
        mx2 = dx+(TILESIZE/4);
        my2 = dy+(TILESIZE/4);
    }
    else {
        mx1 = dx-(TILESIZE/4);
        my1 = dy+(TILESIZE/4);
        mx2 = dx+(TILESIZE/4);
        my2 = dy-(TILESIZE/4);
    }
    draw_thick_line(dr,(float)(TILESIZE/16),mx1,my1,mx2,my2,
                    hflash ? COL_FLASH : COL_TEXT);
    draw_update(dr,dx-(TILESIZE/2)+1,dy-(TILESIZE/2)+1,TILESIZE-1,TILESIZE-1);

    return;
}

static void draw_big_monster(drawing *dr, game_drawstate *ds,
                             const game_state *state, int x, int y,
                             bool hflash, int monster)
{
    int dx,dy;
    char buf[10];
    dx = BORDER+(x* ds->tilesize)+(TILESIZE/2);
    dy = BORDER+(y* ds->tilesize)+(TILESIZE/2)+TILESIZE;
    if (ds->ascii) {
        if (monster == 1) sprintf(buf,"G");
        else if (monster == 2) sprintf(buf,"V");
        else if (monster == 4) sprintf(buf,"Z");
        else sprintf(buf," ");
        draw_text(dr,dx,dy,FONT_FIXED,TILESIZE/2,ALIGN_HCENTRE|ALIGN_VCENTRE,
                  hflash ? COL_FLASH : COL_TEXT,buf);
        draw_update(dr,dx-(TILESIZE/2)+2,dy-(TILESIZE/2)+2,TILESIZE-3,
                    TILESIZE-3);
    }
    else {
        draw_monster(dr, ds, dx, dy, 3*TILESIZE/4, hflash, monster);
    }
    return;
}

static void draw_pencils(drawing *dr, game_drawstate *ds,
                         const game_state *state, int x, int y, int pencil)
{
    int dx, dy;
    int monsters[4];
    int i, j, px, py;
    char buf[10];
    dx = BORDER+(x* ds->tilesize)+(TILESIZE/4);
    dy = BORDER+(y* ds->tilesize)+(TILESIZE/4)+TILESIZE;

    for (i = 0, j = 1; j < 8; j *= 2)
        if (pencil & j)
            monsters[i++] = j;
    while (i < 4)
        monsters[i++] = 0;

    for (py = 0; py < 2; py++)
        for (px = 0; px < 2; px++)
            if (monsters[py*2+px]) {
                if (!ds->ascii) {
                    draw_monster(dr, ds,
                                 dx + TILESIZE/2 * px, dy + TILESIZE/2 * py,
                                 TILESIZE/2, false, monsters[py*2+px]);
                }
                else {
                    switch (monsters[py*2+px]) {
                      case 1: sprintf(buf,"G"); break;
                      case 2: sprintf(buf,"V"); break;
                      case 4: sprintf(buf,"Z"); break;
                    }
                    draw_text(dr,dx + TILESIZE/2 * px,dy + TILESIZE/2 * py,
                              FONT_FIXED,TILESIZE/4,ALIGN_HCENTRE|ALIGN_VCENTRE,
                              COL_TEXT,buf);
                }
            }
    draw_update(dr,dx-(TILESIZE/4)+2,dy-(TILESIZE/4)+2,
                (TILESIZE/2)-3,(TILESIZE/2)-3);

    return;
}

#define FLASH_TIME 0.7F

static bool is_hint_stale(const game_drawstate *ds, bool hflash,
                          const game_state *state, int index)
{
    bool ret = false;
    if (!ds->started) ret = true;
    if (ds->hflash != hflash) ret = true;

    if (ds->hint_errors[index] != state->hint_errors[index]) {
        ds->hint_errors[index] = state->hint_errors[index];
        ret = true;
    }

    if (ds->hints_done[index] != state->hints_done[index]) {
        ds->hints_done[index] = state->hints_done[index];
        ret = true;
    }

    return ret;
}

static void game_redraw(drawing *dr, game_drawstate *ds,
                        const game_state *oldstate, const game_state *state,
                        int dir, const game_ui *ui,
                        float animtime, float flashtime)
{
    int i,j,x,y,xy;
    int xi, c;
    bool stale, hflash, hchanged, changed_ascii;

    hflash = (int)(flashtime * 5 / FLASH_TIME) % 2;

    /* Draw static grid components at startup */    
    if (!ds->started) { 
        draw_rect(dr, BORDER+TILESIZE-1, BORDER+2*TILESIZE-1,
                  (ds->w)*TILESIZE +3, (ds->h)*TILESIZE +3, COL_GRID);
        for (i=0;i<ds->w;i++)
            for (j=0;j<ds->h;j++)
                draw_rect(dr, BORDER+(ds->tilesize*(i+1))+1,
                          BORDER+(ds->tilesize*(j+2))+1, ds->tilesize-1,
                          ds->tilesize-1, COL_BACKGROUND);
        draw_update(dr, 0, 0, 2*BORDER+(ds->w+2)*TILESIZE,
                    2*BORDER+(ds->h+3)*TILESIZE);
    }

    hchanged = false;
    if (ds->hx != ui->hx || ds->hy != ui->hy ||
        ds->hshow != ui->hshow || ds->hpencil != ui->hpencil)
        hchanged = true;

    if (ds->ascii != ui->ascii) {
        ds->ascii = ui->ascii;
        changed_ascii = true;
    } else
        changed_ascii = false;

    /* Draw monster count hints */

    for (i=0;i<3;i++) {
        stale = false;
        if (!ds->started) stale = true;
        if (ds->hflash != hflash) stale = true;
        if (changed_ascii) stale = true;
        if (ds->count_errors[i] != state->count_errors[i]) {
            stale = true;
            ds->count_errors[i] = state->count_errors[i];
        }
        
        if (stale) {
            draw_monster_count(dr, ds, state, i, hflash);
        }
    }

    /* Draw path count hints */
    for (i=0;i<state->common->num_paths;i++) {
        struct path *path = &state->common->paths[i];
        
        if (is_hint_stale(ds, hflash, state, path->grid_start)) {
            draw_path_hint(dr, ds, &state->common->params, path->grid_start,
                           hflash, path->sightings_start);
        }

        if (is_hint_stale(ds, hflash, state, path->grid_end)) {
            draw_path_hint(dr, ds, &state->common->params, path->grid_end,
                           hflash, path->sightings_end);
        }
    }

    /* Draw puzzle grid contents */
    for (x = 1; x < ds->w+1; x++)
        for (y = 1; y < ds->h+1; y++) {
            stale = false;
            xy = x+y*(state->common->params.w+2);
            xi = state->common->xinfo[xy];
            c = state->common->grid[xy];
    
            if (!ds->started) stale = true;
            if (ds->hflash != hflash) stale = true;
            if (changed_ascii) stale = true;
        
            if (hchanged) {
                if ((x == ui->hx && y == ui->hy) ||
                    (x == ds->hx && y == ds->hy))
                    stale = true;
            }

            if (xi >= 0 && (state->guess[xi] != ds->monsters[xi]) ) {
                stale = true;
                ds->monsters[xi] = state->guess[xi];
            }
        
            if (xi >= 0 && (state->pencils[xi] != ds->pencils[xi]) ) {
                stale = true;
                ds->pencils[xi] = state->pencils[xi];
            }

            if (state->cell_errors[xy] != ds->cell_errors[xy]) {
                stale = true;
                ds->cell_errors[xy] = state->cell_errors[xy];
            }
                
            if (stale) {
                draw_cell_background(dr, ds, state, ui, x, y);
                if (xi < 0) 
                    draw_mirror(dr, ds, state, x, y, hflash, c);
                else if (state->guess[xi] == 1 || state->guess[xi] == 2 ||
                         state->guess[xi] == 4)
                    draw_big_monster(dr, ds, state, x, y, hflash, state->guess[xi]);
                else 
                    draw_pencils(dr, ds, state, x, y, state->pencils[xi]);
            }
        }

    ds->hx = ui->hx; ds->hy = ui->hy;
    ds->hshow = ui->hshow;
    ds->hpencil = ui->hpencil;
    ds->hflash = hflash;
    ds->started = true;
    return;
}

static float game_anim_length(const game_state *oldstate,
                              const game_state *newstate, int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(const game_state *oldstate,
                               const game_state *newstate, int dir, game_ui *ui)
{
    return (!oldstate->solved && newstate->solved && !oldstate->cheated &&
            !newstate->cheated) ? FLASH_TIME : 0.0F;
}

static void game_get_cursor_location(const game_ui *ui,
                                     const game_drawstate *ds,
                                     const game_state *state,
                                     const game_params *params,
                                     int *x, int *y, int *w, int *h)
{
    if(ui->hshow) {
        *x = BORDER + (ui->hx) * TILESIZE;
        *y = BORDER + (ui->hy + 1) * TILESIZE;
        *w = *h = TILESIZE;
    }
}

static int game_status(const game_state *state)
{
    return state->solved;
}

#ifdef COMBINED
#define thegame undead
#endif

const struct game thegame = {
    "Undead", "games.undead", "undead",
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
    game_request_keys,
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
    false, false, NULL, NULL,          /* print_size, print */
    false,                 /* wants_statusbar */
    false, NULL,                       /* timing_state */
    0,                     /* flags */
};
