/*
 * dsf.c: some functions to handle a disjoint set forest,
 * which is a data structure useful in any solver which has to
 * worry about avoiding closed loops.
 */

#include <assert.h>
#include <string.h>

#include "puzzles.h"

struct DSF {
    int size;
    int *p;
};

void dsf_reinit(DSF *dsf)
{
    int i;

    for (i = 0; i < dsf->size; i++)
        dsf->p[i] = 6;
    /* Bottom bit of each element of this array stores whether that
     * element is opposite to its parent, which starts off as
     * false. Second bit of each element stores whether that element
     * is the root of its tree or not.  If it's not the root, the
     * remaining 30 bits are the parent, otherwise the remaining 30
     * bits are the number of elements in the tree.  */
}

void dsf_copy(DSF *to, DSF *from)
{
    assert(to->size == from->size && "Mismatch in dsf_copy");
    memcpy(to->p, from->p, to->size * sizeof(int));
}

DSF *snew_dsf(int size)
{
    DSF *ret = snew(DSF);
    ret->size = size;
    ret->p = snewn(size, int);

    dsf_reinit(ret);

    return ret;
}

void dsf_free(DSF *dsf)
{
    if (dsf) {
        sfree(dsf->p);
        sfree(dsf);
    }
}

int dsf_canonify(DSF *dsf, int index)
{
    return edsf_canonify(dsf, index, NULL);
}

void dsf_merge(DSF *dsf, int v1, int v2)
{
    edsf_merge(dsf, v1, v2, false);
}

int dsf_size(DSF *dsf, int index) {
    return dsf->p[dsf_canonify(dsf, index)] >> 2;
}

int edsf_canonify(DSF *dsf, int index, bool *inverse_return)
{
    int start_index = index, canonical_index;
    bool inverse = false;

    assert(0 <= index && index < dsf->size && "Overrun in edsf_canonify");

    /* Find the index of the canonical element of the 'equivalence class' of
     * which start_index is a member, and figure out whether start_index is the
     * same as or inverse to that. */
    while ((dsf->p[index] & 2) == 0) {
        inverse ^= (dsf->p[index] & 1);
	index = dsf->p[index] >> 2;
    }
    canonical_index = index;
    
    if (inverse_return)
        *inverse_return = inverse;
    
    /* Update every member of this 'equivalence class' to point directly at the
     * canonical member. */
    index = start_index;
    while (index != canonical_index) {
	int nextindex = dsf->p[index] >> 2;
        bool nextinverse = inverse ^ (dsf->p[index] & 1);
	dsf->p[index] = (canonical_index << 2) | inverse;
        inverse = nextinverse;
	index = nextindex;
    }

    assert(!inverse);

    return index;
}

void edsf_merge(DSF *dsf, int v1, int v2, bool inverse)
{
    bool i1, i2;

    assert(0 <= v1 && v1 < dsf->size && "Overrun in edsf_merge");
    assert(0 <= v2 && v2 < dsf->size && "Overrun in edsf_merge");

    v1 = edsf_canonify(dsf, v1, &i1);
    assert(dsf->p[v1] & 2);
    inverse ^= i1;
    v2 = edsf_canonify(dsf, v2, &i2);
    assert(dsf->p[v2] & 2);
    inverse ^= i2;

    if (v1 == v2)
        assert(!inverse);
    else {
	/*
	 * We always make the smaller of v1 and v2 the new canonical
	 * element. This ensures that the canonical element of any
	 * class in this structure is always the first element in
	 * it. 'Keen' depends critically on this property.
	 *
	 * (Jonas Koelker previously had this code choosing which
	 * way round to connect the trees by examining the sizes of
	 * the classes being merged, so that the root of the
	 * larger-sized class became the new root. This gives better
	 * asymptotic performance, but I've changed it to do it this
	 * way because I like having a deterministic canonical
	 * element.)
	 */
	if (v1 > v2) {
	    int v3 = v1;
	    v1 = v2;
	    v2 = v3;
	}
	dsf->p[v1] += (dsf->p[v2] >> 2) << 2;
	dsf->p[v2] = (v1 << 2) | inverse;
    }
    
    v2 = edsf_canonify(dsf, v2, &i2);
    assert(v2 == v1);
    assert(i2 == inverse);
}
