#include <stdio.h>
#include <string.h>
#include <time.h>

#include "puzzles.h"
#include "latin.h"

static const char *quis;

#define ELT(sq,x,y) (sq[((y)*order)+(x)])

static void latin_print(digit *sq, int order)
{
    int x, y;

    for (y = 0; y < order; y++) {
	for (x = 0; x < order; x++) {
	    printf("%2u ", ELT(sq, x, y));
	}
	printf("\n");
    }
    printf("\n");
}

static void gen(int order, random_state *rs, int debug)
{
    digit *sq;

    sq = latin_generate(order, rs);
    latin_print(sq, order);
    if (latin_check(sq, order)) {
	fprintf(stderr, "Square is not a latin square!");
	exit(1);
    }

    sfree(sq);
}

static void test_soak(int order, random_state *rs)
{
    digit *sq;
    int n = 0;
    time_t tt_start, tt_now, tt_last;

    tt_now = tt_start = time(NULL);

    while(1) {
        sq = latin_generate(order, rs);
        sfree(sq);
        n++;

        tt_last = time(NULL);
        if (tt_last > tt_now) {
            tt_now = tt_last;
            printf("%d total, %3.1f/s\n", n,
                   (double)n / (double)(tt_now - tt_start));
        }
    }
}

static void usage_exit(const char *msg)
{
    if (msg)
        fprintf(stderr, "%s: %s\n", quis, msg);
    fprintf(stderr, "Usage: %s [--seed SEED] --soak <params> | [game_id [game_id ...]]\n", quis);
    exit(1);
}

int main(int argc, char *argv[])
{
    int i, soak = 0;
    random_state *rs;
    time_t seed = time(NULL);

    quis = argv[0];
    while (--argc > 0) {
	const char *p = *++argv;
	if (!strcmp(p, "--soak"))
	    soak = 1;
	else if (!strcmp(p, "--seed")) {
	    if (argc == 0)
		usage_exit("--seed needs an argument");
	    seed = (time_t)atoi(*++argv);
	    argc--;
	} else if (*p == '-')
		usage_exit("unrecognised option");
	else
	    break; /* finished options */
    }

    rs = random_new((void*)&seed, sizeof(time_t));

    if (soak == 1) {
	if (argc != 1) usage_exit("only one argument for --soak");
	test_soak(atoi(*argv), rs);
    } else {
	if (argc > 0) {
	    for (i = 0; i < argc; i++) {
		gen(atoi(*argv++), rs, 1);
	    }
	} else {
	    while (1) {
		i = random_upto(rs, 20) + 1;
		gen(i, rs, 0);
	    }
	}
    }
    random_free(rs);
    return 0;
}
