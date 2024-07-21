/*
 * Implement arraysort() defined in puzzles.h.
 *
 * Strategy: heapsort.
 */

#include <stddef.h>
#include <string.h>

#include "puzzles.h"

#define PTR(i) ((char *)array + size * (i))
#define SWAP(i,j) swap_regions(PTR(i), PTR(j), size)
#define CMP(i,j) cmp(PTR(i), PTR(j), ctx)

#define LCHILD(i) (2*(i)+1)
#define RCHILD(i) (2*(i)+2)
#define PARENT(i) (((i)-1)/2)

static void downheap(void *array, size_t nmemb, size_t size,
                     arraysort_cmpfn_t cmp, void *ctx, size_t i)
{
    while (LCHILD(i) < nmemb) {
        /* Identify the smallest element out of i and its children. */
        size_t j = i;
        if (CMP(j, LCHILD(i)) < 0)
            j = LCHILD(i);
        if (RCHILD(i) < nmemb &&
            CMP(j, RCHILD(i)) < 0)
            j = RCHILD(i);

        if (j == i)
            return; /* smallest element is already where it should be */

        SWAP(j, i);
        i = j;
    }
}

void arraysort_fn(void *array, size_t nmemb, size_t size,
                  arraysort_cmpfn_t cmp, void *ctx)
{
    size_t i;

    if (nmemb < 2)
        return;                        /* trivial */

    /*
     * Stage 1: build the heap.
     *
     * Linear-time if we do it by downheaping the elements in
     * decreasing order of index, instead of the more obvious approach
     * of upheaping in increasing order. (Also, it means we don't need
     * the upheap function at all.)
     *
     * We don't need to downheap anything in the second half of the
     * array, because it can't have any children to swap with anyway.
     */
    for (i = PARENT(nmemb-1) + 1; i-- > 0 ;)
        downheap(array, nmemb, size, cmp, ctx, i);

    /*
     * Stage 2: dismantle the heap by repeatedly swapping the root
     * element (at index 0) into the last position and then
     * downheaping the new root.
     */
    for (i = nmemb-1; i > 0; i--) {
        SWAP(0, i);
        downheap(array, i, size, cmp, ctx, 0);
    }
}
