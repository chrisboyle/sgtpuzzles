/*
 * group.c: a Latin-square puzzle, but played with groups' Cayley
 * tables. That is, you are given a Cayley table of a group with
 * most elements blank and a few clues, and you must fill it in
 * so as to preserve the group axioms.
 *
 * This is a perfectly playable and fully working puzzle, but I'm
 * leaving it for the moment in the 'unfinished' directory because
 * it's just too esoteric (not to mention _hard_) for me to be
 * comfortable presenting it to the general public as something they
 * might (implicitly) actually want to play.
 *
 * TODO:
 *
 *  - more solver techniques?
 *     * Inverses: once we know that gh = e, we can immediately
 * 	 deduce hg = e as well; then for any gx=y we can deduce
 * 	 hy=x, and for any xg=y we have yh=x.
 *     * Hard-mode associativity: we currently deduce based on
 * 	 definite numbers in the grid, but we could also winnow
 * 	 based on _possible_ numbers.
 *     * My overambitious original thoughts included wondering if we
 * 	 could infer that there must be elements of certain orders
 * 	 (e.g. a group of order divisible by 5 must contain an
 * 	 element of order 5), but I think in fact this is probably
 * 	 silly.
 *
 *  - a mode which shuffles the identity element into the mix
 *    instead of keeping it clearly shown for you?
 *     * shuffle more fully during table generation
 *     * start clue removal by clearing the identity row and column
 * 	 completely, or else it'll be totally obvious where it is
 *     * have to print the group elements outside the grid
 *     * new_ui should start the cursor at 0,0 not 1,1, and cursor
 * 	 should not be constrained to x,y >= 1
 *     * get rid of the COL_IDENTITY highlights
 *     * will we need more checks in check_errors?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "latin.h"

/*
 * Difficulty levels. I do some macro ickery here to ensure that my
 * enum and the various forms of my name list always match up.
 */
#define DIFFLIST(A) \
    A(TRIVIAL,Trivial,NULL,t) \
    A(NORMAL,Normal,solver_normal,n) \
    A(HARD,Hard,NULL,h) \
    A(EXTREME,Extreme,NULL,x) \
    A(UNREASONABLE,Unreasonable,NULL,u)
#define ENUM(upper,title,func,lower) DIFF_ ## upper,
#define TITLE(upper,title,func,lower) #title,
#define ENCODE(upper,title,func,lower) #lower
#define CONFIG(upper,title,func,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const group_diffnames[] = { DIFFLIST(TITLE) };
static char const group_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

enum {
    COL_BACKGROUND,
    COL_IDENTITY,
    COL_GRID,
    COL_USER,
    COL_HIGHLIGHT,
    COL_ERROR,
    COL_PENCIL,
    NCOLOURS
};

#define FROMCHAR(c) ((c)>='0'&&(c)<='9' ? (c)-'0' : \
			 (c)>='A'&&(c)<='Z' ? (c)-'A'+10 : (c)-'a'+10)
#define ISCHAR(c) (((c)>='0'&&(c)<='9') || \
		       ((c)>='A'&&(c)<='Z') || ((c)>='a'&&(c)<='z'))
#define TOCHAR(c) ((c)>=10 ? (c)-10+'a' : (c)+'0')

struct game_params {
    int w, diff;
};

struct game_state {
    game_params par;
    digit *grid;
    unsigned char *immutable;
    int *pencil;		       /* bitmaps using bits 1<<1..1<<n */
    int completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    ret->w = 6;
    ret->diff = DIFF_NORMAL;

    return ret;
}

const static struct game_params group_presets[] = {
    {  4, DIFF_NORMAL         },
    {  6, DIFF_NORMAL         },
};

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(group_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = group_presets[i]; /* structure copy */

    sprintf(buf, "%dx%d %s", ret->w, ret->w, group_diffnames[ret->diff]);

    *name = dupstr(buf);
    *params = ret;
    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;		       /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    char const *p = string;

    params->w = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;

    if (*p == 'd') {
        int i;
        p++;
        params->diff = DIFFCOUNT+1; /* ...which is invalid */
        if (*p) {
            for (i = 0; i < DIFFCOUNT; i++) {
                if (*p == group_diffchars[i])
                    params->diff = i;
            }
            p++;
        }
    }
}

static char *encode_params(game_params *params, int full)
{
    char ret[80];

    sprintf(ret, "%d", params->w);
    if (full)
        sprintf(ret + strlen(ret), "d%c", group_diffchars[params->diff]);

    return dupstr(ret);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(3, config_item);

    ret[0].name = "Grid size";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Difficulty";
    ret[1].type = C_CHOICES;
    ret[1].sval = DIFFCONFIG;
    ret[1].ival = params->diff;

    ret[2].name = NULL;
    ret[2].type = C_END;
    ret[2].sval = NULL;
    ret[2].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w = atoi(cfg[0].sval);
    ret->diff = cfg[1].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if (params->w < 3 || params->w > 31)
        return "Grid size must be between 3 and 31";
    if (params->diff >= DIFFCOUNT)
        return "Unknown difficulty rating";
    return NULL;
}

/* ----------------------------------------------------------------------
 * Solver.
 */

static int solver_normal(struct latin_solver *solver, void *vctx)
{
    int w = solver->o;
    digit *grid = solver->grid;
    int i, j, k;

    /*
     * Deduce using associativity: (ab)c = a(bc).
     *
     * So we pick any a,b,c we like; then if we know ab, bc, and
     * (ab)c we can fill in a(bc).
     */
    for (i = 1; i < w; i++)
	for (j = 1; j < w; j++)
	    for (k = 1; k < w; k++) {
		if (!grid[i*w+j] || !grid[j*w+k])
		    continue;
		if (grid[(grid[i*w+j]-1)*w+k] &&
		    !grid[i*w+(grid[j*w+k]-1)]) {
		    int x = grid[j*w+k]-1, y = i;
		    int n = grid[(grid[i*w+j]-1)*w+k];
#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			printf("%*sassociativity on %d,%d,%d: %d*%d = %d*%d\n",
			       solver_recurse_depth*4, "",
			       i+1, j+1, k+1,
			       grid[i*w+j], k+1, i+1, grid[j*w+k]);
			printf("%*s  placing %d at (%d,%d)\n",
			       solver_recurse_depth*4, "",
			       n, x+1, y+1);
		    }
#endif
		    if (solver->cube[(x*w+y)*w+n-1]) {
			latin_solver_place(solver, x, y, n);
			return 1;
		    } else {
#ifdef STANDALONE_SOLVER
			if (solver_show_working)
			    printf("%*s  contradiction!\n",
				   solver_recurse_depth*4, "");
			return -1;
#endif
		    }
		}
		if (!grid[(grid[i*w+j]-1)*w+k] &&
		    grid[i*w+(grid[j*w+k]-1)]) {
		    int x = k, y = grid[i*w+j]-1;
		    int n = grid[i*w+(grid[j*w+k]-1)];
#ifdef STANDALONE_SOLVER
		    if (solver_show_working) {
			printf("%*sassociativity on %d,%d,%d: %d*%d = %d*%d\n",
			       solver_recurse_depth*4, "",
			       i+1, j+1, k+1,
			       grid[i*w+j], k+1, i+1, grid[j*w+k]);
			printf("%*s  placing %d at (%d,%d)\n",
			       solver_recurse_depth*4, "",
			       n, x+1, y+1);
		    }
#endif
		    if (solver->cube[(x*w+y)*w+n-1]) {
			latin_solver_place(solver, x, y, n);
			return 1;
		    } else {
#ifdef STANDALONE_SOLVER
			if (solver_show_working)
			    printf("%*s  contradiction!\n",
				   solver_recurse_depth*4, "");
			return -1;
#endif
		    }
		}
	    }

    return 0;
}

#define SOLVER(upper,title,func,lower) func,
static usersolver_t const group_solvers[] = { DIFFLIST(SOLVER) };

static int solver(int w, digit *grid, int maxdiff)
{
    int ret;
    
    ret = latin_solver(grid, w, maxdiff,
		       DIFF_TRIVIAL, DIFF_HARD, DIFF_EXTREME,
		       DIFF_EXTREME, DIFF_UNREASONABLE,
		       group_solvers, NULL, NULL, NULL);

    return ret;
}

/* ----------------------------------------------------------------------
 * Grid generation.
 */

static char *encode_grid(char *desc, digit *grid, int area)
{
    int run, i;
    char *p = desc;

    run = 0;
    for (i = 0; i <= area; i++) {
	int n = (i < area ? grid[i] : -1);

	if (!n)
	    run++;
	else {
	    if (run) {
		while (run > 0) {
		    int c = 'a' - 1 + run;
		    if (run > 26)
			c = 'z';
		    *p++ = c;
		    run -= c - ('a' - 1);
		}
	    } else {
		/*
		 * If there's a number in the very top left or
		 * bottom right, there's no point putting an
		 * unnecessary _ before or after it.
		 */
		if (p > desc && n > 0)
		    *p++ = '_';
	    }
	    if (n > 0)
		p += sprintf(p, "%d", n);
	    run = 0;
	}
    }
    return p;
}

/* ----- data generated by group.gap begins ----- */

struct group {
    unsigned long autosize;
    int order, ngens;
    const char *gens;
};
struct groups {
    int ngroups;
    const struct group *groups;
};

static const struct group groupdata[] = {
    /* order 2 */
    {1L, 2, 1, "21"},
    /* order 3 */
    {2L, 3, 1, "231"},
    /* order 4 */
    {2L, 4, 1, "2341"},
    {6L, 4, 2, "2143" "3412"},
    /* order 5 */
    {4L, 5, 1, "23451"},
    /* order 6 */
    {6L, 6, 2, "365214" "214365"},
    {2L, 6, 1, "436521"},
    /* order 7 */
    {6L, 7, 1, "2345671"},
    /* order 8 */
    {4L, 8, 1, "23564781"},
    {8L, 8, 2, "24567183" "57284361"},
    {8L, 8, 2, "57284361" "21563487"},
    {24L, 8, 2, "24567183" "38472516"},
    {168L, 8, 3, "21563487" "35172846" "46718235"},
    /* order 9 */
    {6L, 9, 1, "245378691"},
    {48L, 9, 2, "245178396" "356781924"},
    /* order 10 */
    {20L, 10, 2, "3A52749618" "21436587A9"},
    {4L, 10, 1, "436587A921"},
    /* order 11 */
    {10L, 11, 1, "23456789AB1"},
    /* order 12 */
    {12L, 12, 2, "7C4BA5832916" "2356179A4BC8"},
    {4L, 12, 1, "589AB32C4761"},
    {24L, 12, 2, "256719AB34C8" "6A2B8C574391"},
    {12L, 12, 2, "7C4BA5832916" "2156349A78CB"},
    {12L, 12, 2, "649A78C2B153" "794B6C83A512"},
    /* order 13 */
    {12L, 13, 1, "23456789ABCD1"},
    /* order 14 */
    {42L, 14, 2, "5C7E92B4D6183A" "21436587A9CBED"},
    {6L, 14, 1, "6587A9CBED2143"},
    /* order 15 */
    {8L, 15, 1, "5783AB6DE9F2C41"},
    /* order 16 */
    {8L, 16, 1, "DBEG6F1427C3958A"},
    {96L, 16, 2, "9CB3FE6G54A87D12" "2467891BCDE3F5GA"},
    {32L, 16, 2, "D98G643FE2C1BA75" "25678AB1CDEF34G9"},
    {32L, 16, 2, "9613F7CD45A2EGB8" "25678AB1CDEF34G9"},
    {16L, 16, 2, "DF8G6B39E2C14A75" "2467895BCDEAF1G3"},
    {16L, 16, 2, "D98G64AFE2C5B371" "2467895BCDEAF1G3"},
    {32L, 16, 2, "DF8G6439E2C5BA71" "21678345CDE9ABGF"},
    {16L, 16, 2, "D98G6BAFE2C14375" "74G8EF5B6C2391DA"},
    {32L, 16, 2, "D92G64AF78C5B3E1" "3C59A7DGB1F8E642"},
    {192L, 16, 3,
     "D38G619AE2C45F7B" "25678AB1CDEF34G9" "7BC2EF546G8A91D3"},
    {64L, 16, 3, "D38G619AE2C45F7B" "CF76GBA92ED54381" "3D19A8G645FE2CB7"},
    {192L, 16, 3,
     "9GB3F7DC54A2E618" "25678AB1CDEF34G9" "3D59A2G6B1F78C4E"},
    {48L, 16, 3, "9G4AFE6C5B327D18" "6A2CD5F378GB19E4" "4795BC8EAF1DG236"},
    {20160L, 16, 4,
     "58AB1DE2F34G679C" "21678345CDE9ABGF" "3619A2CD45F78GBE"
     "4791BC2E3F56G8AD"},
    /* order 17 */
    {16L, 17, 1, "56789ABCDEFGH1234"},
    /* order 18 */
    {54L, 18, 2, "DB9HFGE17CI5342A86" "215634ABC789FGDEIH"},
    {6L, 18, 1, "53AB786FG4DECI9H21"},
    {12L, 18, 2, "53AB782FG1DE6I4HC9" "BEFGH36I5978CA1D24"},
    {432L, 18, 3,
     "96E1BCH34FG278I5DA" "EFH36I978BCA1DG245" "215634ABC789FGDEIH"},
    {48L, 18, 2, "53AB782FG1DE6I4HC9" "64BC89FG2DE1I5H3A7"},
    /* order 19 */
    {18L, 19, 1, "56789ABCDEFGHIJ1234"},
    /* order 20 */
    {40L, 20, 2, "7K4BI58F29CJ6DG3AH1E" "5129346D78AHBCEKFGIJ"},
    {8L, 20, 1, "589AC3DEG7HIJB2K4F61"},
    {20L, 20, 2, "4AJ8HE3CKI7G52B196FD" "5129346D78AHBCEKFGIJ"},
    {40L, 20, 2, "7K4BI58F29CJ6DG3AH1E" "5329176D4BAH8FEKCJIG"},
    {24L, 20, 2, "976D4BAH8FEKCJI5G321" "649A78DEBCHIFGK2J153"},
    /* order 21 */
    {42L, 21, 2, "9KCJ2FL5I4817B3AE6DHG" "5A8CDBFGEIJH1LK342679"},
    {12L, 21, 1, "5783AB6DE9GHCJKFL2I41"},
    /* order 22 */
    {110L, 22, 2, "5K7M92B4D6F8HAJCLE1G3I" "21436587A9CBEDGFIHKJML"},
    {10L, 22, 1, "6587A9CBEDGFIHKJML2143"},
    /* order 23 */
    {22L, 23, 1, "56789ABCDEFGHIJKLMN1234"},
    /* order 24 */
    {24L, 24, 2, "HO5ANGLDBCI9M26KJ1378E4F" "8IEFGJN3KLM2C49AO671BHD5"},
    {8L, 24, 1, "DH2KL4IN678OA5C9EFGBJ1M3"},
    {24L, 24, 2, "9FHI25LM6N78BC1ODEGJ34KA" "EAOFM74BJDK69GH5C3LI2N18"},
    {48L, 24, 2, "HL5ANMO6BCI9G7DEJ132FK48" "8JEFGNC4KLM2I91BO673H5DA"},
    {24L, 24, 2, "HO5ANGLDBCI9M26KJ1378E4F" "KN8EOCI9FGLDJ13HM2645A7B"},
    {48L, 24, 2, "HL5ANMO6BCI9G7DEJ132FK48" "21678345DEFG9ABCKLMHIJON"},
    {48L, 24, 3,
     "HOBANMLD5JI9G76KC432FE18" "AL5HIGO6BCN3M2DEJ1978K4F"
     "8JEFGNC4KLM2I91BO673H5DA"},
    {24L, 24, 3,
     "HLBANGO65JI9M2DEC4378K1F" "AO5HIMLDBCN3G76KJ192FE48"
     "KIFEOCN38MLDJ19AG7645H2B"},
    {16L, 24, 2, "DI7KLCN9FG6OJ4AH2ME5B381" "MBO8FH1JEKG23N45L679ACDI"},
    {16L, 24, 2, "DI7KLCN9FG6OJ4AH2ME5B381" "IDCN97KLJ4AHFG6O5B32ME18"},
    {48L, 24, 2, "9LCHI7ODJ43NFGEK5BA2M618" "7CDFGIJ4KL2MN95B6O8AH1E3"},
    {24L, 24, 2, "LAGODI3JE87KCN9B6M254HF1" "EIL6MCN9GODFA54H87K3J12B"},
    {24L, 24, 2, "D92KL1HI678O345NEFGABCMJ" "FBOM6NJ37LKE4IHA2GD1C985"},
    {144L, 24, 3,
     "HOBANMLD5JI9G76KC432FE18" "AL5HIGO6BCN3M2DEJ1978K4F"
     "21678345DEFG9ABCKLMHIJON"},
    {336L, 24, 3,
     "HKBANFEO5JI98MLDC43G7612" "AE5HI8KLBCN3FGO6J19M2D47"
     "85EFGABCKLM2HIJ1O67N34D9"},
    /* order 25 */
    {20L, 25, 1, "589CDEGHIJ6KLM2ANO4FP71B3"},
    {480L, 25, 2, "589CDEGHIJ3KLM26NO4AP7FB1" "245789BCDE1GHIJ3KLM6NOAPF"},
    /* order 26 */
    {156L, 26, 2,
     "5O7Q92B4D6F8HAJCLENGPI1K3M" "21436587A9CBEDGFIHKJMLONQP"},
    {12L, 26, 1, "6587A9CBEDGFIHKJMLONQP2143"},
    /* order 27 */
    {18L, 27, 1, "53BC689IJKDE4GHOP7LMANRFQ12"},
    {108L, 27, 2,
     "54BC79AIJKEFGH1OPLM2N3RQ68D" "DI2LG5O67Q4NBCREF9A8JKMH1P3"},
    {432L, 27, 2,
     "51BC234IJK6789AOPDEFGHRLMNQ" "3E89PLM1GHRB7Q64NKIJFDA5O2C"},
    {54L, 27, 2,
     "54BC79AIJKEFGH1OPLM2N3RQ68D" "DR2LNKI67QA8P5OEFH1GBCM34J9"},
    {11232L, 27, 3,
     "51BC234IJK6789AOPDEFGHRLMNQ" "3689BDE1GHIJ2LM4N5OP7QACRFK"
     "479ACEFGH1JKLM2N3OP5Q68RBDI"},
    /* order 28 */
    {84L, 28, 2,
     "7S4BQ58F29CJ6DGNAHKRELO3IP1M" "5129346D78AHBCELFGIPJKMSNOQR"},
    {12L, 28, 1, "589AC3DEG7HIKBLMOFPQRJ2S4N61"},
    {84L, 28, 2,
     "7S4BQ58F29CJ6DGNAHKRELO3IP1M" "5329176D4BAH8FELCJIPGNMSKRQO"},
    {36L, 28, 2,
     "976D4BAH8FELCJIPGNMSKRQ5O321" "649A78DEBCHIFGLMJKPQNOS2R153"},
    /* order 29 */
    {28L, 29, 1, "56789ABCDEFGHIJKLMNOPQRST1234"},
    /* order 30 */
    {24L, 30, 2,
     "LHQ7NOTDERSA9JK6UGF1PBCM34I285" "BFHIL3NO5Q78RSATDE6UG9JKCM1P24"},
    {40L, 30, 2,
     "DU4JOA89PS2GEFT56MKL7BCRQ1HI3N" "BQGHT36MNL78CRS1DEIU54JKOA9P2F"},
    {120L, 30, 2,
     "DS4JU589POABEFT2GHKL76MNQ1CR3I" "215634ABC789GHIDEFMNOJKLRSPQUT"},
    {8L, 30, 1, "HEMNJKCRS9PQIU5FT3OABL782G1D64"},
    /* order 31 */
    {30L, 31, 1, "56789ABCDEFGHIJKLMNOPQRSTUV1234"},
};

static const struct groups groups[] = {
    {0, NULL},			/* trivial case: 0 */
    {0, NULL},			/* trivial case: 1 */
    {1, groupdata + 0},		/* 2 */
    {1, groupdata + 1},		/* 3 */
    {2, groupdata + 2},		/* 4 */
    {1, groupdata + 4},		/* 5 */
    {2, groupdata + 5},		/* 6 */
    {1, groupdata + 7},		/* 7 */
    {5, groupdata + 8},		/* 8 */
    {2, groupdata + 13},	/* 9 */
    {2, groupdata + 15},	/* 10 */
    {1, groupdata + 17},	/* 11 */
    {5, groupdata + 18},	/* 12 */
    {1, groupdata + 23},	/* 13 */
    {2, groupdata + 24},	/* 14 */
    {1, groupdata + 26},	/* 15 */
    {14, groupdata + 27},	/* 16 */
    {1, groupdata + 41},	/* 17 */
    {5, groupdata + 42},	/* 18 */
    {1, groupdata + 47},	/* 19 */
    {5, groupdata + 48},	/* 20 */
    {2, groupdata + 53},	/* 21 */
    {2, groupdata + 55},	/* 22 */
    {1, groupdata + 57},	/* 23 */
    {15, groupdata + 58},	/* 24 */
    {2, groupdata + 73},	/* 25 */
    {2, groupdata + 75},	/* 26 */
    {5, groupdata + 77},	/* 27 */
    {4, groupdata + 82},	/* 28 */
    {1, groupdata + 86},	/* 29 */
    {4, groupdata + 87},	/* 30 */
    {1, groupdata + 91},	/* 31 */
};

/* ----- data generated by group.gap ends ----- */

static char *new_game_desc(game_params *params, random_state *rs,
			   char **aux, int interactive)
{
    int w = params->w, a = w*w;
    digit *grid, *soln, *soln2;
    int *indices;
    int i, j, k, qh, qt;
    int diff = params->diff;
    const struct group *group;
    char *desc, *p;

    /*
     * Difficulty exceptions: some combinations of size and
     * difficulty cannot be satisfied, because all puzzles of at
     * most that difficulty are actually even easier.
     *
     * Remember to re-test this whenever a change is made to the
     * solver logic!
     *
     * I tested it using the following shell command:

for d in t n h x u; do
  for i in {3..9}; do
    echo ./group --generate 1 ${i}d${d}
    perl -e 'alarm 30; exec @ARGV' ./group --generate 5 ${i}d${d} >/dev/null \
      || echo broken
  done
done

     * Of course, it's better to do that after taking the exceptions
     * _out_, so as to detect exceptions that should be removed as
     * well as those which should be added.
     */
    if (w <= 9 && diff == DIFF_EXTREME)
	diff--;
    if (w <= 6 && diff == DIFF_HARD)
	diff--;
    if (w <= 4 && diff > DIFF_TRIVIAL)
	diff = DIFF_TRIVIAL;

    grid = snewn(a, digit);
    soln = snewn(a, digit);
    soln2 = snewn(a, digit);
    indices = snewn(a, int);

    while (1) {
	/*
	 * Construct a valid group table, by picking a group from
	 * the above data table, decompressing it into a full
	 * representation by BFS, and then randomly permuting its
	 * non-identity elements.
	 *
	 * We build the canonical table in 'soln' (and use 'grid' as
	 * our BFS queue), then transfer the table into 'grid'
	 * having shuffled the rows.
	 */
	assert(w >= 2);
	assert(w < lenof(groups));
	group = groups[w].groups + random_upto(rs, groups[w].ngroups);
	assert(group->order == w);
	memset(soln, 0, a);
	for (i = 0; i < w; i++)
	    soln[i] = i+1;
	qh = qt = 0;
	grid[qt++] = 1;
	while (qh < qt) {
	    digit *row, *newrow;

	    i = grid[qh++];
	    row = soln + (i-1)*w;

	    for (j = 0; j < group->ngens; j++) {
		int nri;
		const char *gen = group->gens + j*w;

		/*
		 * Apply each group generator to row, constructing a
		 * new row.
		 */
		nri = FROMCHAR(gen[row[0]-1]);   /* which row is it? */
		newrow = soln + (nri-1)*w;
		if (!newrow[0]) {   /* not done yet */
		    for (k = 0; k < w; k++)
			newrow[k] = FROMCHAR(gen[row[k]-1]);
		    grid[qt++] = nri;
		}
	    }
	}
	/* That's got the canonical table. Now shuffle it. */
	for (i = 0; i < w; i++)
	    grid[i] = i+1;
	shuffle(grid+1, w-1, sizeof(*grid), rs);
	for (i = 1; i < w; i++)
	    for (j = 0; j < w; j++)
		grid[(grid[i]-1)*w+(grid[j]-1)] = grid[soln[i*w+j]-1];
	for (i = 0; i < w; i++)
	    grid[i] = i+1;

	/*
	 * Remove entries one by one while the puzzle is still
	 * soluble at the appropriate difficulty level.
	 */
	memcpy(soln, grid, a);

	k = 0;
	for (i = 1; i < w; i++)
	    for (j = 1; j < w; j++)
		indices[k++] = i*w+j;
	shuffle(indices, k, sizeof(*indices), rs);

	for (i = 0; i < k; i++) {
	    memcpy(soln2, grid, a);
	    soln2[indices[i]] = 0;
	    if (solver(w, soln2, diff) <= diff)
		grid[indices[i]] = 0;
	}

	/*
	 * Make sure the puzzle isn't too easy.
	 */
	if (diff > 0) {
	    memcpy(soln2, grid, a);
	    if (solver(w, soln2, diff-1) < diff)
		continue;	       /* go round and try again */
	}

	/*
	 * Done.
	 */
	break;
    }

    /*
     * Encode the puzzle description.
     */
    desc = snewn(a*20, char);
    p = encode_grid(desc, grid, a);
    *p++ = '\0';
    desc = sresize(desc, p - desc, char);

    /*
     * Encode the solution.
     */
    *aux = snewn(a+2, char);
    (*aux)[0] = 'S';
    for (i = 0; i < a; i++)
	(*aux)[i+1] = TOCHAR(soln[i]);
    (*aux)[a+1] = '\0';

    sfree(grid);
    sfree(soln);
    sfree(soln2);
    sfree(indices);

    return desc;
}

/* ----------------------------------------------------------------------
 * Gameplay.
 */

static char *validate_grid_desc(const char **pdesc, int range, int area)
{
    const char *desc = *pdesc;
    int squares = 0;
    while (*desc && *desc != ',') {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            squares += n - 'a' + 1;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            int val = atoi(desc-1);
            if (val < 1 || val > range)
                return "Out-of-range number in game description";
            squares++;
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else
            return "Invalid character in game description";
    }

    if (squares < area)
        return "Not enough data to fill grid";

    if (squares > area)
        return "Too much data to fit in grid";
    *pdesc = desc;
    return NULL;
}

static char *validate_desc(game_params *params, char *desc)
{
    int w = params->w, a = w*w;
    const char *p = desc;

    return validate_grid_desc(&p, w, a);
}

static char *spec_to_grid(char *desc, digit *grid, int area)
{
    int i = 0;
    while (*desc && *desc != ',') {
        int n = *desc++;
        if (n >= 'a' && n <= 'z') {
            int run = n - 'a' + 1;
            assert(i + run <= area);
            while (run-- > 0)
                grid[i++] = 0;
        } else if (n == '_') {
            /* do nothing */;
        } else if (n > '0' && n <= '9') {
            assert(i < area);
            grid[i++] = atoi(desc-1);
            while (*desc >= '0' && *desc <= '9')
                desc++;
        } else {
            assert(!"We can't get here");
        }
    }
    assert(i == area);
    return desc;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    int w = params->w, a = w*w;
    game_state *state = snew(game_state);
    int i;

    state->par = *params;	       /* structure copy */
    state->grid = snewn(a, digit);
    state->immutable = snewn(a, unsigned char);
    state->pencil = snewn(a, int);
    for (i = 0; i < a; i++) {
	state->grid[i] = 0;
	state->immutable[i] = 0;
	state->pencil[i] = 0;
    }

    desc = spec_to_grid(desc, state->grid, a);
    for (i = 0; i < a; i++)
	if (state->grid[i] != 0)
	    state->immutable[i] = TRUE;

    state->completed = state->cheated = FALSE;

    return state;
}

static game_state *dup_game(game_state *state)
{
    int w = state->par.w, a = w*w;
    game_state *ret = snew(game_state);

    ret->par = state->par;	       /* structure copy */

    ret->grid = snewn(a, digit);
    ret->immutable = snewn(a, unsigned char);
    ret->pencil = snewn(a, int);
    memcpy(ret->grid, state->grid, a*sizeof(digit));
    memcpy(ret->immutable, state->immutable, a*sizeof(unsigned char));
    memcpy(ret->pencil, state->pencil, a*sizeof(int));

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state->immutable);
    sfree(state->pencil);
    sfree(state);
}

static char *solve_game(game_state *state, game_state *currstate,
			char *aux, char **error)
{
    int w = state->par.w, a = w*w;
    int i, ret;
    digit *soln;
    char *out;

    if (aux)
	return dupstr(aux);

    soln = snewn(a, digit);
    memcpy(soln, state->grid, a*sizeof(digit));

    ret = solver(w, soln, DIFFCOUNT-1);

    if (ret == diff_impossible) {
	*error = "No solution exists for this puzzle";
	out = NULL;
    } else if (ret == diff_ambiguous) {
	*error = "Multiple solutions exist for this puzzle";
	out = NULL;
    } else {
	out = snewn(a+2, char);
	out[0] = 'S';
	for (i = 0; i < a; i++)
	    out[i+1] = TOCHAR(soln[i]);
	out[a+1] = '\0';
    }

    sfree(soln);
    return out;
}

static int game_can_format_as_text_now(game_params *params)
{
    return TRUE;
}

static char *game_text_format(game_state *state)
{
    int w = state->par.w;
    int x, y;
    char *ret, *p, ch;

    ret = snewn(2*w*w+1, char);	       /* leave room for terminating NUL */

    p = ret;
    for (y = 0; y < w; y++) {
	for (x = 0; x < w; x++) {
	    digit d = state->grid[y*w+x];

            if (d == 0) {
		ch = '.';
	    } else {
		ch = TOCHAR(d);
	    }

	    *p++ = ch;
	    if (x == w-1) {
		*p++ = '\n';
	    } else {
		*p++ = ' ';
	    }
	}
    }

    assert(p - ret == 2*w*w);
    *p = '\0';
    return ret;
}

struct game_ui {
    /*
     * These are the coordinates of the currently highlighted
     * square on the grid, if hshow = 1.
     */
    int hx, hy;
    /*
     * This indicates whether the current highlight is a
     * pencil-mark one or a real one.
     */
    int hpencil;
    /*
     * This indicates whether or not we're showing the highlight
     * (used to be hx = hy = -1); important so that when we're
     * using the cursor keys it doesn't keep coming back at a
     * fixed position. When hshow = 1, pressing a valid number
     * or letter key or Space will enter that number or letter in the grid.
     */
    int hshow;
    /*
     * This indicates whether we're using the highlight as a cursor;
     * it means that it doesn't vanish on a keypress, and that it is
     * allowed on immutable squares.
     */
    int hcursor;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ui = snew(game_ui);

    ui->hx = ui->hy = 1;
    ui->hpencil = ui->hshow = ui->hcursor = 0;

    return ui;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
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
    int w = newstate->par.w;
    /*
     * We prevent pencil-mode highlighting of a filled square, unless
     * we're using the cursor keys. So if the user has just filled in
     * a square which we had a pencil-mode highlight in (by Undo, or
     * by Redo, or by Solve), then we cancel the highlight.
     */
    if (ui->hshow && ui->hpencil && !ui->hcursor &&
        newstate->grid[ui->hy * w + ui->hx] != 0) {
        ui->hshow = 0;
    }
}

#define PREFERRED_TILESIZE 48
#define TILESIZE (ds->tilesize)
#define BORDER (TILESIZE / 2)
#define GRIDEXTRA max((TILESIZE / 32),1)
#define COORD(x) ((x)*TILESIZE + BORDER)
#define FROMCOORD(x) (((x)+(TILESIZE-BORDER)) / TILESIZE - 1)

#define FLASH_TIME 0.4F

#define DF_HIGHLIGHT 0x0400
#define DF_HIGHLIGHT_PENCIL 0x0200
#define DF_IMMUTABLE 0x0100
#define DF_DIGIT_MASK 0x001F

#define EF_DIGIT_SHIFT 5
#define EF_DIGIT_MASK ((1 << EF_DIGIT_SHIFT) - 1)
#define EF_LEFT_SHIFT 0
#define EF_RIGHT_SHIFT (3*EF_DIGIT_SHIFT)
#define EF_LEFT_MASK ((1UL << (3*EF_DIGIT_SHIFT)) - 1UL)
#define EF_RIGHT_MASK (EF_LEFT_MASK << EF_RIGHT_SHIFT)
#define EF_LATIN (1UL << (6*EF_DIGIT_SHIFT))

struct game_drawstate {
    int w, tilesize;
    int started;
    long *tiles, *pencil, *errors;
    long *errtmp;
};

static int check_errors(game_state *state, long *errors)
{
    int w = state->par.w, a = w*w;
    digit *grid = state->grid;
    int i, j, k, x, y, errs = FALSE;

    if (errors)
	for (i = 0; i < a; i++)
	    errors[i] = 0;

    for (y = 0; y < w; y++) {
	unsigned long mask = 0, errmask = 0;
	for (x = 0; x < w; x++) {
	    unsigned long bit = 1UL << grid[y*w+x];
	    errmask |= (mask & bit);
	    mask |= bit;
	}

	if (mask != (1 << (w+1)) - (1 << 1)) {
	    errs = TRUE;
	    errmask &= ~1UL;
	    if (errors) {
		for (x = 0; x < w; x++)
		    if (errmask & (1UL << grid[y*w+x]))
			errors[y*w+x] |= EF_LATIN;
	    }
	}
    }

    for (x = 0; x < w; x++) {
	unsigned long mask = 0, errmask = 0;
	for (y = 0; y < w; y++) {
	    unsigned long bit = 1UL << grid[y*w+x];
	    errmask |= (mask & bit);
	    mask |= bit;
	}

	if (mask != (1 << (w+1)) - (1 << 1)) {
	    errs = TRUE;
	    errmask &= ~1UL;
	    if (errors) {
		for (y = 0; y < w; y++)
		    if (errmask & (1UL << grid[y*w+x]))
			errors[y*w+x] |= EF_LATIN;
	    }
	}
    }

    for (i = 1; i < w; i++)
	for (j = 1; j < w; j++)
	    for (k = 1; k < w; k++)
		if (grid[i*w+j] && grid[j*w+k] &&
		    grid[(grid[i*w+j]-1)*w+k] &&
		    grid[i*w+(grid[j*w+k]-1)] &&
		    grid[(grid[i*w+j]-1)*w+k] != grid[i*w+(grid[j*w+k]-1)]) {
		    if (errors) {
			int a = i+1, b = j+1, c = k+1;
			int ab = grid[i*w+j], bc = grid[j*w+k];
			int left = (ab-1)*w+(c-1), right = (a-1)*w+(bc-1);
			/*
			 * If the appropriate error slot is already
			 * used for one of the squares, we don't
			 * fill either of them.
			 */
			if (!(errors[left] & EF_LEFT_MASK) &&
			    !(errors[right] & EF_RIGHT_MASK)) {
			    long err;
			    err = a;
			    err = (err << EF_DIGIT_SHIFT) | b;
			    err = (err << EF_DIGIT_SHIFT) | c;
			    errors[left] |= err << EF_LEFT_SHIFT;
			    errors[right] |= err << EF_RIGHT_SHIFT;
			}
		    }
		    errs = TRUE;
		}

    return errs;
}

static char *interpret_move(game_state *state, game_ui *ui, game_drawstate *ds,
			    int x, int y, int button)
{
    int w = state->par.w;
    int tx, ty;
    char buf[80];

    button &= ~MOD_MASK;

    tx = FROMCOORD(x);
    ty = FROMCOORD(y);

    if (tx > 0 && tx < w && ty > 0 && ty < w) {
        if (button == LEFT_BUTTON) {
	    if (tx == ui->hx && ty == ui->hy &&
		ui->hshow && ui->hpencil == 0) {
                ui->hshow = 0;
            } else {
                ui->hx = tx;
                ui->hy = ty;
		ui->hshow = !state->immutable[ty*w+tx];
                ui->hpencil = 0;
            }
            ui->hcursor = 0;
            return "";		       /* UI activity occurred */
        }
        if (button == RIGHT_BUTTON) {
            /*
             * Pencil-mode highlighting for non filled squares.
             */
            if (state->grid[ty*w+tx] == 0) {
                if (tx == ui->hx && ty == ui->hy &&
                    ui->hshow && ui->hpencil) {
                    ui->hshow = 0;
                } else {
                    ui->hpencil = 1;
                    ui->hx = tx;
                    ui->hy = ty;
                    ui->hshow = 1;
                }
            } else {
                ui->hshow = 0;
            }
            ui->hcursor = 0;
            return "";		       /* UI activity occurred */
        }
    }
    if (IS_CURSOR_MOVE(button)) {
	ui->hx--; ui->hy--;
        move_cursor(button, &ui->hx, &ui->hy, w-1, w-1, 0);
	ui->hx++; ui->hy++;
        ui->hshow = ui->hcursor = 1;
        return "";
    }
    if (ui->hshow &&
        (button == CURSOR_SELECT)) {
        ui->hpencil = 1 - ui->hpencil;
        ui->hcursor = 1;
        return "";
    }

    if (ui->hshow &&
	((ISCHAR(button) && FROMCHAR(button) <= w) ||
	 button == CURSOR_SELECT2 || button == '\b')) {
	int n = FROMCHAR(button);
	if (button == CURSOR_SELECT2 || button == '\b')
	    n = 0;

        /*
         * Can't make pencil marks in a filled square. This can only
         * become highlighted if we're using cursor keys.
         */
        if (ui->hpencil && state->grid[ui->hy*w+ui->hx])
            return NULL;

	/*
	 * Can't do anything to an immutable square.
	 */
        if (state->immutable[ui->hy*w+ui->hx])
            return NULL;

	sprintf(buf, "%c%d,%d,%d",
		(char)(ui->hpencil && n > 0 ? 'P' : 'R'), ui->hx, ui->hy, n);

        if (!ui->hcursor) ui->hshow = 0;

	return dupstr(buf);
    }

    if (button == 'M' || button == 'm')
        return dupstr("M");

    return NULL;
}

static game_state *execute_move(game_state *from, char *move)
{
    int w = from->par.w, a = w*w;
    game_state *ret;
    int x, y, i, n;

    if (move[0] == 'S') {
	ret = dup_game(from);
	ret->completed = ret->cheated = TRUE;

	for (i = 0; i < a; i++) {
	    if (!ISCHAR(move[i+1]) || FROMCHAR(move[i+1]) > w) {
		free_game(ret);
		return NULL;
	    }
	    ret->grid[i] = FROMCHAR(move[i+1]);
	    ret->pencil[i] = 0;
	}

	if (move[a+1] != '\0') {
	    free_game(ret);
	    return NULL;
	}

	return ret;
    } else if ((move[0] == 'P' || move[0] == 'R') &&
	sscanf(move+1, "%d,%d,%d", &x, &y, &n) == 3 &&
	x >= 0 && x < w && y >= 0 && y < w && n >= 0 && n <= w) {
	if (from->immutable[y*w+x])
	    return NULL;

	ret = dup_game(from);
        if (move[0] == 'P' && n > 0) {
            ret->pencil[y*w+x] ^= 1 << n;
        } else {
            ret->grid[y*w+x] = n;
            ret->pencil[y*w+x] = 0;

            if (!ret->completed && !check_errors(ret, NULL))
                ret->completed = TRUE;
        }
	return ret;
    } else if (move[0] == 'M') {
	/*
	 * Fill in absolutely all pencil marks everywhere. (I
	 * wouldn't use this for actual play, but it's a handy
	 * starting point when following through a set of
	 * diagnostics output by the standalone solver.)
	 */
	ret = dup_game(from);
	for (i = 0; i < a; i++) {
	    if (!ret->grid[i])
		ret->pencil[i] = (1 << (w+1)) - (1 << 1);
	}
	return ret;
    } else
	return NULL;		       /* couldn't parse move string */
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

#define SIZE(w) ((w) * TILESIZE + 2*BORDER)

static void game_compute_size(game_params *params, int tilesize,
			      int *x, int *y)
{
    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    struct { int tilesize; } ads, *ds = &ads;
    ads.tilesize = tilesize;

    *x = *y = SIZE(params->w);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
			  game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    ret[COL_GRID * 3 + 0] = 0.0F;
    ret[COL_GRID * 3 + 1] = 0.0F;
    ret[COL_GRID * 3 + 2] = 0.0F;

    ret[COL_IDENTITY * 3 + 0] = 0.89F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_IDENTITY * 3 + 1] = 0.89F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_IDENTITY * 3 + 2] = 0.89F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_USER * 3 + 0] = 0.0F;
    ret[COL_USER * 3 + 1] = 0.6F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_USER * 3 + 2] = 0.0F;

    ret[COL_HIGHLIGHT * 3 + 0] = 0.78F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_HIGHLIGHT * 3 + 1] = 0.78F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_HIGHLIGHT * 3 + 2] = 0.78F * ret[COL_BACKGROUND * 3 + 2];

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_PENCIL * 3 + 0] = 0.5F * ret[COL_BACKGROUND * 3 + 0];
    ret[COL_PENCIL * 3 + 1] = 0.5F * ret[COL_BACKGROUND * 3 + 1];
    ret[COL_PENCIL * 3 + 2] = ret[COL_BACKGROUND * 3 + 2];

    *ncolours = NCOLOURS;
    return ret;
}

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    int w = state->par.w, a = w*w;
    struct game_drawstate *ds = snew(struct game_drawstate);
    int i;

    ds->w = w;
    ds->tilesize = 0;
    ds->started = FALSE;
    ds->tiles = snewn(a, long);
    ds->pencil = snewn(a, long);
    ds->errors = snewn(a, long);
    for (i = 0; i < a; i++)
	ds->tiles[i] = ds->pencil[i] = -1;
    ds->errtmp = snewn(a, long);

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->tiles);
    sfree(ds->pencil);
    sfree(ds->errors);
    sfree(ds->errtmp);
    sfree(ds);
}

static void draw_tile(drawing *dr, game_drawstate *ds, int x, int y, long tile,
		      long pencil, long error)
{
    int w = ds->w /* , a = w*w */;
    int tx, ty, tw, th;
    int cx, cy, cw, ch;
    char str[64];

    tx = BORDER + x * TILESIZE + 1;
    ty = BORDER + y * TILESIZE + 1;

    cx = tx;
    cy = ty;
    cw = tw = TILESIZE-1;
    ch = th = TILESIZE-1;

    clip(dr, cx, cy, cw, ch);

    /* background needs erasing */
    draw_rect(dr, cx, cy, cw, ch,
	      (tile & DF_HIGHLIGHT) ? COL_HIGHLIGHT :
	      (x == 0 || y == 0) ? COL_IDENTITY : COL_BACKGROUND);

    /* pencil-mode highlight */
    if (tile & DF_HIGHLIGHT_PENCIL) {
        int coords[6];
        coords[0] = cx;
        coords[1] = cy;
        coords[2] = cx+cw/2;
        coords[3] = cy;
        coords[4] = cx;
        coords[5] = cy+ch/2;
        draw_polygon(dr, coords, 3, COL_HIGHLIGHT, COL_HIGHLIGHT);
    }

    /* new number needs drawing? */
    if (tile & DF_DIGIT_MASK) {
	str[1] = '\0';
	str[0] = TOCHAR(tile & DF_DIGIT_MASK);
	draw_text(dr, tx + TILESIZE/2, ty + TILESIZE/2,
		  FONT_VARIABLE, TILESIZE/2, ALIGN_VCENTRE | ALIGN_HCENTRE,
		  (error & EF_LATIN) ? COL_ERROR :
		  (tile & DF_IMMUTABLE) ? COL_GRID : COL_USER, str);

	if (error & EF_LEFT_MASK) {
	    int a = (error >> (EF_LEFT_SHIFT+2*EF_DIGIT_SHIFT))&EF_DIGIT_MASK;
	    int b = (error >> (EF_LEFT_SHIFT+1*EF_DIGIT_SHIFT))&EF_DIGIT_MASK;
	    int c = (error >> (EF_LEFT_SHIFT                 ))&EF_DIGIT_MASK;
	    char buf[10];
	    sprintf(buf, "(%c%c)%c", TOCHAR(a), TOCHAR(b), TOCHAR(c));
	    draw_text(dr, tx + TILESIZE/2, ty + TILESIZE/6,
		      FONT_VARIABLE, TILESIZE/6, ALIGN_VCENTRE | ALIGN_HCENTRE,
		      COL_ERROR, buf);
	}
	if (error & EF_RIGHT_MASK) {
	    int a = (error >> (EF_RIGHT_SHIFT+2*EF_DIGIT_SHIFT))&EF_DIGIT_MASK;
	    int b = (error >> (EF_RIGHT_SHIFT+1*EF_DIGIT_SHIFT))&EF_DIGIT_MASK;
	    int c = (error >> (EF_RIGHT_SHIFT                 ))&EF_DIGIT_MASK;
	    char buf[10];
	    sprintf(buf, "%c(%c%c)", TOCHAR(a), TOCHAR(b), TOCHAR(c));
	    draw_text(dr, tx + TILESIZE/2, ty + TILESIZE - TILESIZE/6,
		      FONT_VARIABLE, TILESIZE/6, ALIGN_VCENTRE | ALIGN_HCENTRE,
		      COL_ERROR, buf);
	}
    } else {
        int i, j, npencil;
	int pl, pr, pt, pb;
	float bestsize;
	int pw, ph, minph, pbest, fontsize;

        /* Count the pencil marks required. */
        for (i = 1, npencil = 0; i <= w; i++)
            if (pencil & (1 << i))
		npencil++;
	if (npencil) {

	    minph = 2;

	    /*
	     * Determine the bounding rectangle within which we're going
	     * to put the pencil marks.
	     */
	    /* Start with the whole square */
	    pl = tx + GRIDEXTRA;
	    pr = pl + TILESIZE - GRIDEXTRA;
	    pt = ty + GRIDEXTRA;
	    pb = pt + TILESIZE - GRIDEXTRA;

	    /*
	     * We arrange our pencil marks in a grid layout, with
	     * the number of rows and columns adjusted to allow the
	     * maximum font size.
	     *
	     * So now we work out what the grid size ought to be.
	     */
	    bestsize = 0.0;
	    pbest = 0;
	    /* Minimum */
	    for (pw = 3; pw < max(npencil,4); pw++) {
		float fw, fh, fs;

		ph = (npencil + pw - 1) / pw;
		ph = max(ph, minph);
		fw = (pr - pl) / (float)pw;
		fh = (pb - pt) / (float)ph;
		fs = min(fw, fh);
		if (fs > bestsize) {
		    bestsize = fs;
		    pbest = pw;
		}
	    }
	    assert(pbest > 0);
	    pw = pbest;
	    ph = (npencil + pw - 1) / pw;
	    ph = max(ph, minph);

	    /*
	     * Now we've got our grid dimensions, work out the pixel
	     * size of a grid element, and round it to the nearest
	     * pixel. (We don't want rounding errors to make the
	     * grid look uneven at low pixel sizes.)
	     */
	    fontsize = min((pr - pl) / pw, (pb - pt) / ph);

	    /*
	     * Centre the resulting figure in the square.
	     */
	    pl = tx + (TILESIZE - fontsize * pw) / 2;
	    pt = ty + (TILESIZE - fontsize * ph) / 2;

	    /*
	     * Now actually draw the pencil marks.
	     */
	    for (i = 1, j = 0; i <= w; i++)
		if (pencil & (1 << i)) {
		    int dx = j % pw, dy = j / pw;

		    str[1] = '\0';
		    str[0] = TOCHAR(i);
		    draw_text(dr, pl + fontsize * (2*dx+1) / 2,
			      pt + fontsize * (2*dy+1) / 2,
			      FONT_VARIABLE, fontsize,
			      ALIGN_VCENTRE | ALIGN_HCENTRE, COL_PENCIL, str);
		    j++;
		}
	}
    }

    unclip(dr);

    draw_update(dr, cx, cy, cw, ch);
}

static void game_redraw(drawing *dr, game_drawstate *ds, game_state *oldstate,
			game_state *state, int dir, game_ui *ui,
			float animtime, float flashtime)
{
    int w = state->par.w /*, a = w*w */;
    int x, y;

    if (!ds->started) {
	/*
	 * The initial contents of the window are not guaranteed and
	 * can vary with front ends. To be on the safe side, all
	 * games should start by drawing a big background-colour
	 * rectangle covering the whole window.
	 */
	draw_rect(dr, 0, 0, SIZE(w), SIZE(w), COL_BACKGROUND);

	/*
	 * Big containing rectangle.
	 */
	draw_rect(dr, COORD(0) - GRIDEXTRA, COORD(0) - GRIDEXTRA,
		  w*TILESIZE+1+GRIDEXTRA*2, w*TILESIZE+1+GRIDEXTRA*2,
		  COL_GRID);

	draw_update(dr, 0, 0, SIZE(w), SIZE(w));

	ds->started = TRUE;
    }

    check_errors(state, ds->errtmp);

    for (y = 0; y < w; y++) {
	for (x = 0; x < w; x++) {
	    long tile = 0L, pencil = 0L, error;

	    if (state->grid[y*w+x])
		tile = state->grid[y*w+x];
	    else
		pencil = (long)state->pencil[y*w+x];

	    if (state->immutable[y*w+x])
		tile |= DF_IMMUTABLE;

	    if (ui->hshow && ui->hx == x && ui->hy == y)
		tile |= (ui->hpencil ? DF_HIGHLIGHT_PENCIL : DF_HIGHLIGHT);

            if (flashtime > 0 &&
                (flashtime <= FLASH_TIME/3 ||
                 flashtime >= FLASH_TIME*2/3))
                tile |= DF_HIGHLIGHT;  /* completion flash */

	    error = ds->errtmp[y*w+x];

	    if (ds->tiles[y*w+x] != tile ||
		ds->pencil[y*w+x] != pencil ||
		ds->errors[y*w+x] != error) {
		ds->tiles[y*w+x] = tile;
		ds->pencil[y*w+x] = pencil;
		ds->errors[y*w+x] = error;
		draw_tile(dr, ds, x, y, tile, pencil, error);
	    }
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
    if (!oldstate->completed && newstate->completed &&
	!oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
    return 0.0F;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    if (state->completed)
	return FALSE;
    return TRUE;
}

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /*
     * We use 9mm squares by default, like Solo.
     */
    game_compute_size(params, 900, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int w = state->par.w;
    int ink = print_mono_colour(dr, 0);
    int ehighlight = print_grey_colour(dr, 0.90F);
    int x, y;

    /* Ick: fake up `ds->tilesize' for macro expansion purposes */
    game_drawstate ads, *ds = &ads;
    game_set_size(dr, ds, NULL, tilesize);

    /*
     * Highlight the identity row and column.
     */
    for (x = 1; x < w; x++)
	draw_rect(dr, BORDER + x*TILESIZE, BORDER,
		  TILESIZE, TILESIZE, ehighlight);
    for (y = 0; y < w; y++)
	draw_rect(dr, BORDER, BORDER + y*TILESIZE,
		  TILESIZE, TILESIZE, ehighlight);

    /*
     * Border.
     */
    print_line_width(dr, 3 * TILESIZE / 40);
    draw_rect_outline(dr, BORDER, BORDER, w*TILESIZE, w*TILESIZE, ink);

    /*
     * Main grid.
     */
    for (x = 1; x < w; x++) {
	print_line_width(dr, TILESIZE / 40);
	draw_line(dr, BORDER+x*TILESIZE, BORDER,
		  BORDER+x*TILESIZE, BORDER+w*TILESIZE, ink);
    }
    for (y = 1; y < w; y++) {
	print_line_width(dr, TILESIZE / 40);
	draw_line(dr, BORDER, BORDER+y*TILESIZE,
		  BORDER+w*TILESIZE, BORDER+y*TILESIZE, ink);
    }

    /*
     * Numbers.
     */
    for (y = 0; y < w; y++)
	for (x = 0; x < w; x++)
	    if (state->grid[y*w+x]) {
		char str[2];
		str[1] = '\0';
		str[0] = TOCHAR(state->grid[y*w+x]);
		draw_text(dr, BORDER + x*TILESIZE + TILESIZE/2,
			  BORDER + y*TILESIZE + TILESIZE/2,
			  FONT_VARIABLE, TILESIZE/2,
			  ALIGN_VCENTRE | ALIGN_HCENTRE, ink, str);
	    }
}

#ifdef COMBINED
#define thegame group
#endif

const struct game thegame = {
    "Group", NULL, NULL,
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
    TRUE, solve_game,
    TRUE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    PREFERRED_TILESIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    TRUE, FALSE, game_print_size, game_print,
    FALSE,			       /* wants_statusbar */
    FALSE, game_timing_state,
    REQUIRE_RBUTTON | REQUIRE_NUMPAD,  /* flags */
};

#ifdef STANDALONE_SOLVER

#include <stdarg.h>

int main(int argc, char **argv)
{
    game_params *p;
    game_state *s;
    char *id = NULL, *desc, *err;
    digit *grid;
    int grade = FALSE;
    int ret, diff, really_show_working = FALSE;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-v")) {
            really_show_working = TRUE;
        } else if (!strcmp(p, "-g")) {
            grade = TRUE;
        } else if (*p == '-') {
            fprintf(stderr, "%s: unrecognised option `%s'\n", argv[0], p);
            return 1;
        } else {
            id = p;
        }
    }

    if (!id) {
        fprintf(stderr, "usage: %s [-g | -v] <game_id>\n", argv[0]);
        return 1;
    }

    desc = strchr(id, ':');
    if (!desc) {
        fprintf(stderr, "%s: game id expects a colon in it\n", argv[0]);
        return 1;
    }
    *desc++ = '\0';

    p = default_params();
    decode_params(p, id);
    err = validate_desc(p, desc);
    if (err) {
        fprintf(stderr, "%s: %s\n", argv[0], err);
        return 1;
    }
    s = new_game(NULL, p, desc);

    grid = snewn(p->w * p->w, digit);

    /*
     * When solving a Normal puzzle, we don't want to bother the
     * user with Hard-level deductions. For this reason, we grade
     * the puzzle internally before doing anything else.
     */
    ret = -1;			       /* placate optimiser */
    solver_show_working = FALSE;
    for (diff = 0; diff < DIFFCOUNT; diff++) {
	memcpy(grid, s->grid, p->w * p->w);
	ret = solver(p->w, grid, diff);
	if (ret <= diff)
	    break;
    }

    if (diff == DIFFCOUNT) {
	if (grade)
	    printf("Difficulty rating: ambiguous\n");
	else
	    printf("Unable to find a unique solution\n");
    } else {
	if (grade) {
	    if (ret == diff_impossible)
		printf("Difficulty rating: impossible (no solution exists)\n");
	    else
		printf("Difficulty rating: %s\n", group_diffnames[ret]);
	} else {
	    solver_show_working = really_show_working;
	    memcpy(grid, s->grid, p->w * p->w);
	    ret = solver(p->w, grid, diff);
	    if (ret != diff)
		printf("Puzzle is inconsistent\n");
	    else {
		memcpy(s->grid, grid, p->w * p->w);
		fputs(game_text_format(s), stdout);
	    }
	}
    }

    return 0;
}

#endif

/* vim: set shiftwidth=4 tabstop=8: */
