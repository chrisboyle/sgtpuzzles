/*
 * dsf.c: some functions to handle a disjoint set forest,
 * which is a data structure useful in any solver which has to
 * worry about avoiding closed loops.
 */

#include <assert.h>
#include <string.h>

#include "puzzles.h"

/*void print_dsf(int *dsf, int size)
{
    int *printed_elements = snewn(size, int);
    int *equal_elements = snewn(size, int);
    int *inverse_elements = snewn(size, int);
    int printed_count = 0, equal_count, inverse_count;
    int i, n;
    bool inverse;

    memset(printed_elements, -1, sizeof(int) * size);

    while (1) {
        equal_count = 0;
        inverse_count = 0;
        for (i = 0; i < size; ++i) {
            if (!memchr(printed_elements, i, sizeof(int) * size)) 
                break;
        }
        if (i == size)
            goto done;

        i = dsf_canonify(dsf, i);

        for (n = 0; n < size; ++n) {
            if (edsf_canonify(dsf, n, &inverse) == i) {
               if (inverse)
                   inverse_elements[inverse_count++] = n;
               else
                   equal_elements[equal_count++] = n;
            }
        }
        
        for (n = 0; n < equal_count; ++n) {
            fprintf(stderr, "%d ", equal_elements[n]);
            printed_elements[printed_count++] = equal_elements[n];
        }
        if (inverse_count) {
            fprintf(stderr, "!= ");
            for (n = 0; n < inverse_count; ++n) {
                fprintf(stderr, "%d ", inverse_elements[n]);
                printed_elements[printed_count++] = inverse_elements[n];
            }
        }
        fprintf(stderr, "\n");
    }
done:

    sfree(printed_elements);
    sfree(equal_elements);
    sfree(inverse_elements);
}*/

void dsf_init(int *dsf, int size)
{
    int i;

    for (i = 0; i < size; i++) dsf[i] = 6;
    /* Bottom bit of each element of this array stores whether that
     * element is opposite to its parent, which starts off as
     * false. Second bit of each element stores whether that element
     * is the root of its tree or not.  If it's not the root, the
     * remaining 30 bits are the parent, otherwise the remaining 30
     * bits are the number of elements in the tree.  */
}

int *snew_dsf(int size)
{
    int *ret;
    
    ret = snewn(size, int);
    dsf_init(ret, size);

    /*print_dsf(ret, size); */

    return ret;
}

int dsf_canonify(int *dsf, int index)
{
    return edsf_canonify(dsf, index, NULL);
}

void dsf_merge(int *dsf, int v1, int v2)
{
    edsf_merge(dsf, v1, v2, false);
}

int dsf_size(int *dsf, int index) {
    return dsf[dsf_canonify(dsf, index)] >> 2;
}

int edsf_canonify(int *dsf, int index, bool *inverse_return)
{
    int start_index = index, canonical_index;
    bool inverse = false;

/*    fprintf(stderr, "dsf = %p\n", dsf); */
/*    fprintf(stderr, "Canonify %2d\n", index); */

    assert(index >= 0);

    /* Find the index of the canonical element of the 'equivalence class' of
     * which start_index is a member, and figure out whether start_index is the
     * same as or inverse to that. */
    while ((dsf[index] & 2) == 0) {
        inverse ^= (dsf[index] & 1);
	index = dsf[index] >> 2;
/*        fprintf(stderr, "index = %2d, ", index); */
/*        fprintf(stderr, "inverse = %d\n", inverse); */
    }
    canonical_index = index;
    
    if (inverse_return)
        *inverse_return = inverse;
    
    /* Update every member of this 'equivalence class' to point directly at the
     * canonical member. */
    index = start_index;
    while (index != canonical_index) {
	int nextindex = dsf[index] >> 2;
        bool nextinverse = inverse ^ (dsf[index] & 1);
	dsf[index] = (canonical_index << 2) | inverse;
        inverse = nextinverse;
	index = nextindex;
    }

    assert(!inverse);

/*    fprintf(stderr, "Return %2d\n", index); */
    
    return index;
}

void edsf_merge(int *dsf, int v1, int v2, bool inverse)
{
    bool i1, i2;

/*    fprintf(stderr, "dsf = %p\n", dsf); */
/*    fprintf(stderr, "Merge [%2d,%2d], %d\n", v1, v2, inverse); */
    
    v1 = edsf_canonify(dsf, v1, &i1);
    assert(dsf[v1] & 2);
    inverse ^= i1;
    v2 = edsf_canonify(dsf, v2, &i2);
    assert(dsf[v2] & 2);
    inverse ^= i2;

/*    fprintf(stderr, "Doing [%2d,%2d], %d\n", v1, v2, inverse); */

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
	dsf[v1] += (dsf[v2] >> 2) << 2;
	dsf[v2] = (v1 << 2) | inverse;
    }
    
    v2 = edsf_canonify(dsf, v2, &i2);
    assert(v2 == v1);
    assert(i2 == inverse);

/*    fprintf(stderr, "dsf[%2d] = %2d\n", v2, dsf[v2]); */
}
