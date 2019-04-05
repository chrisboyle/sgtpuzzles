/*
 * Implement arraysort() defined in puzzles.h.
 *
 * Strategy: heapsort.
 */

#include <stddef.h>
#include <string.h>

#include "puzzles.h"

static void memswap(void *av, void *bv, size_t size)
{
    char t[4096];
    char *a = (char *)av, *b = (char *)bv;

    while (size > 0) {
        size_t thissize = size < sizeof(t) ? size : sizeof(t);

        memcpy(t, a, thissize);
        memcpy(a, b, thissize);
        memcpy(b, t, thissize);

        size -= thissize;
        a += thissize;
        b += thissize;
    }
}

#define PTR(i) ((char *)array + size * (i))
#define SWAP(i,j) memswap(PTR(i), PTR(j), size)
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

#ifdef SORT_TEST

#include <stdlib.h>
#include <time.h>

int testcmp(const void *av, const void *bv, void *ctx)
{
    int a = *(const int *)av, b = *(const int *)bv;
    const int *keys = (const int *)ctx;
    return keys[a] < keys[b] ? -1 : keys[a] > keys[b] ? +1 : 0;
}

int resetcmp(const void *av, const void *bv)
{
    int a = *(const int *)av, b = *(const int *)bv;
    return a < b ? -1 : a > b ? +1 : 0;
}

int main(int argc, char **argv)
{
    typedef int Array[3723];
    Array data, keys;
    int iteration;
    unsigned seed;

    seed = (argc > 1 ? strtoul(argv[1], NULL, 0) : time(NULL));
    printf("Random seed = %u\n", seed);
    srand(seed);

    for (iteration = 0; iteration < 10000; iteration++) {
        int j;
        const char *fail = NULL;

        for (j = 0; j < lenof(data); j++) {
            data[j] = j;
            keys[j] = rand();
        }

        arraysort(data, lenof(data), testcmp, keys);

        for (j = 1; j < lenof(data); j++) {
            if (keys[data[j]] < keys[data[j-1]])
                fail = "output misordered";
        }
        if (!fail) {
            Array reset;
            memcpy(reset, data, sizeof(data));
            qsort(reset, lenof(reset), sizeof(*reset), resetcmp);
            for (j = 0; j < lenof(reset); j++)
                if (reset[j] != j)
                    fail = "output not permuted";
        }

        if (fail) {
            printf("Failed at iteration %d: %s\n", iteration, fail);
            printf("Key values:\n");
            for (j = 0; j < lenof(keys); j++)
                printf("  [%2d] %10d\n", j, keys[j]);
            printf("Output sorted order:\n");
            for (j = 0; j < lenof(data); j++)
                printf("  [%2d] %10d\n", data[j], keys[data[j]]);
            return 1;
        }
    }

    printf("OK\n");
    return 0;
}

#endif /* SORT_TEST */
