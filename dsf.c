/*
 * dsf.c: two small functions to handle a disjoint set forest,
 * which is a data structure useful in any solver which has to
 * worry about avoiding closed loops.
 */

#include "puzzles.h"

int dsf_canonify(int *dsf, int val)
{
    int v2 = val;

    while (dsf[val] != val)
	val = dsf[val];

    while (v2 != val) {
	int tmp = dsf[v2];
	dsf[v2] = val;
	v2 = tmp;
    }

    return val;
}

void dsf_merge(int *dsf, int v1, int v2)
{
    v1 = dsf_canonify(dsf, v1);
    v2 = dsf_canonify(dsf, v2);
    dsf[v2] = v1;
}

void dsf_init(int *dsf, int len)
{
    int i;

    for (i = 0; i < len; i++)
	dsf[i] = i;
}
