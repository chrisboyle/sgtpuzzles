/*
 * misc.c: Miscellaneous helpful functions.
 */

#include <assert.h>
#include <stdlib.h>

#include "puzzles.h"

int rand_upto(int limit)
{
    unsigned long divisor = RAND_MAX / (unsigned)limit;
    unsigned long max = divisor * (unsigned)limit;
    unsigned long n;

    assert(limit > 0);

    do {
        n = rand();
    } while (n >= max);

    n /= divisor;

    return (int)n;
}

void free_cfg(config_item *cfg)
{
    config_item *i;

    for (i = cfg; i->type != C_END; i++)
	if (i->type == C_STRING)
	    sfree(i->sval);
    sfree(cfg);
}
