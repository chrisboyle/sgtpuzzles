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
