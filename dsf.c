/*
 * dsf.c: some functions to handle a disjoint set forest,
 * which is a data structure useful in any solver which has to
 * worry about avoiding closed loops.
 */

#include <assert.h>
#include <limits.h>
#include <string.h>

#include "puzzles.h"

#define DSF_INDEX_MASK (UINT_MAX >> 1)
#define DSF_FLAG_CANONICAL (UINT_MAX & ~(UINT_MAX >> 1))
#define DSF_MAX (DSF_INDEX_MASK + 1)

struct DSF {
    /*
     * Size of the dsf.
     */
    size_t size;

    /*
     * Main array storing the data structure.
     *
     * If n is the canonical element of an equivalence class,
     * parent_or_size[n] holds the number of elements in that class,
     * bitwise-ORed with DSF_FLAG_CANONICAL.
     *
     * If n is not the canonical element, parent_or_size[n] holds the
     * index of another element nearer to the root of the tree for
     * that class.
     */
    unsigned *parent_or_size;

    /*
     * Extra storage for flip tracking.
     *
     * If n is not a canonical element, flip[n] indicates whether the
     * sense of this element is flipped relative to parent_or_size[n].
     *
     * If n is a canonical element, flip[n] is unused.
     */
    unsigned char *flip;

    /*
     * Extra storage for minimal-element tracking.
     *
     * If n is a canonical element, min[n] holds the index of the
     * smallest value in n's equivalence class.
     *
     * If n is not a canonical element, min[n] is unused.
     */
    unsigned *min;
};

static DSF *dsf_new_internal(int size, bool flip, bool min)
{
    DSF *dsf;

    assert(0 < size && size <= DSF_MAX && "Bad dsf size");

    dsf = snew(DSF);
    dsf->size = size;
    dsf->parent_or_size = snewn(size, unsigned);
    dsf->flip = flip ? snewn(size, unsigned char) : NULL;
    dsf->min = min ? snewn(size, unsigned) : NULL;

    dsf_reinit(dsf);

    return dsf;
}

DSF *dsf_new(int size)
{
    return dsf_new_internal(size, false, false);
}

DSF *dsf_new_flip(int size)
{
    return dsf_new_internal(size, true, false);
}

DSF *dsf_new_min(int size)
{
    return dsf_new_internal(size, false, true);
}

void dsf_reinit(DSF *dsf)
{
    size_t i;

    /* Every element starts as the root of an equivalence class of size 1 */
    for (i = 0; i < dsf->size; i++)
        dsf->parent_or_size[i] = DSF_FLAG_CANONICAL | 1;

    /* If we're tracking minima then every element is also its own min */
    if (dsf->min)
        for (i = 0; i < dsf->size; i++)
            dsf->min[i] = i;

    /* No need to initialise dsf->flip, even if it exists, because
     * only the entries for non-root elements are meaningful, and
     * currently there are none. */
}

void dsf_copy(DSF *to, DSF *from)
{
    assert(to->size == from->size && "Mismatch in dsf_copy");
    memcpy(to->parent_or_size, from->parent_or_size,
           to->size * sizeof(*to->parent_or_size));
    if (to->flip) {
        assert(from->flip && "Copying a non-flip dsf to a flip one");
        memcpy(to->flip, from->flip, to->size * sizeof(*to->flip));
    }
    if (to->min) {
        assert(from->min && "Copying a non-min dsf to a min one");
        memcpy(to->min, from->min, to->size * sizeof(*to->min));
    }
}


void dsf_free(DSF *dsf)
{
    if (dsf) {
        sfree(dsf->parent_or_size);
        sfree(dsf->flip);
        sfree(dsf->min);
        sfree(dsf);
    }
}

static inline size_t dsf_find_root(DSF *dsf, size_t n)
{
    while (!(dsf->parent_or_size[n] & DSF_FLAG_CANONICAL))
        n = dsf->parent_or_size[n];
    return n;
}

static inline void dsf_path_compress(DSF *dsf, size_t n, size_t root)
{
    while (!(dsf->parent_or_size[n] & DSF_FLAG_CANONICAL)) {
        size_t prev = n;
        n = dsf->parent_or_size[n];
        dsf->parent_or_size[prev] = root;
    }
    assert(n == root);
}

int dsf_canonify(DSF *dsf, int n)
{
    size_t root;

    assert(0 <= n && n < dsf->size && "Overrun in dsf_canonify");

    root = dsf_find_root(dsf, n);
    dsf_path_compress(dsf, n, root);
    return root;
}

void dsf_merge(DSF *dsf, int n1, int n2)
{
    size_t r1, r2, s1, s2, root;

    assert(0 <= n1 && n1 < dsf->size && "Overrun in dsf_merge");
    assert(0 <= n2 && n2 < dsf->size && "Overrun in dsf_merge");
    assert(!dsf->flip && "dsf_merge on a flip dsf");

    /* Find the root elements */
    r1 = dsf_find_root(dsf, n1);
    r2 = dsf_find_root(dsf, n2);

    if (r1 == r2) {
        /* Classes are already the same, so we have a common root */
        root = r1;
    } else {
        /* Classes must be merged */

        /* Decide which one to use as the overall root, based on size */
        s1 = dsf->parent_or_size[r1] & DSF_INDEX_MASK;
        s2 = dsf->parent_or_size[r2] & DSF_INDEX_MASK;
        if (s1 > s2) {
            dsf->parent_or_size[r2] = root = r1;
        } else {
            dsf->parent_or_size[r1] = root = r2;
        }
        dsf->parent_or_size[root] = (s1 + s2) | DSF_FLAG_CANONICAL;

        if (dsf->min) {
            /* Update the min of the merged class */
            unsigned m1 = dsf->min[r1], m2 = dsf->min[r2];
            dsf->min[root] = m1 < m2 ? m1 : m2;
        }
    }

    /* Path-compress both paths from n1 and n2 so they point at the new root */
    dsf_path_compress(dsf, n1, root);
    dsf_path_compress(dsf, n2, root);
}

bool dsf_equivalent(DSF *dsf, int n1, int n2)
{
    return dsf_canonify(dsf, n1) == dsf_canonify(dsf, n2);
}

int dsf_size(DSF *dsf, int n)
{
    size_t root = dsf_canonify(dsf, n);
    return dsf->parent_or_size[root] & DSF_INDEX_MASK;
}

static inline size_t dsf_find_root_flip(DSF *dsf, size_t n, unsigned *flip)
{
    *flip = 0;
    while (!(dsf->parent_or_size[n] & DSF_FLAG_CANONICAL)) {
        *flip ^= dsf->flip[n];
        n = dsf->parent_or_size[n];
    }
    return n;
}

static inline void dsf_path_compress_flip(DSF *dsf, size_t n, size_t root,
                                          unsigned flip)
{
    while (!(dsf->parent_or_size[n] & DSF_FLAG_CANONICAL)) {
        size_t prev = n;
        unsigned flip_prev = flip;
        n = dsf->parent_or_size[n];
        flip ^= dsf->flip[prev];
        dsf->flip[prev] = flip_prev;
        dsf->parent_or_size[prev] = root;
    }
    assert(n == root);
}

int dsf_canonify_flip(DSF *dsf, int n, bool *inverse)
{
    size_t root;
    unsigned flip;

    assert(0 <= n && n < dsf->size && "Overrun in dsf_canonify_flip");
    assert(dsf->flip && "dsf_canonify_flip on a non-flip dsf");

    root = dsf_find_root_flip(dsf, n, &flip);
    dsf_path_compress_flip(dsf, n, root, flip);
    *inverse = flip;
    return root;
}

void dsf_merge_flip(DSF *dsf, int n1, int n2, bool inverse)
{
    size_t r1, r2, s1, s2, root;
    unsigned f1, f2;

    assert(0 <= n1 && n1 < dsf->size && "Overrun in dsf_merge_flip");
    assert(0 <= n2 && n2 < dsf->size && "Overrun in dsf_merge_flip");
    assert(dsf->flip && "dsf_merge_flip on a non-flip dsf");

    /* Find the root elements */
    r1 = dsf_find_root_flip(dsf, n1, &f1);
    r2 = dsf_find_root_flip(dsf, n2, &f2);

    if (r1 == r2) {
        /* Classes are already the same, so we have a common root */
        assert((f1 ^ f2 ^ inverse) == 0 && "Inconsistency in dsf_merge_flip");
        root = r1;
    } else {
        /* Classes must be merged */

        /* Decide which one to use as the overall root, based on size */
        s1 = dsf->parent_or_size[r1] & DSF_INDEX_MASK;
        s2 = dsf->parent_or_size[r2] & DSF_INDEX_MASK;
        if (s1 > s2) {
            dsf->parent_or_size[r2] = root = r1;
            dsf->flip[r2] = f1 ^ f2 ^ inverse;
            f2 ^= dsf->flip[r2];
        } else {
            root = r2;
            dsf->parent_or_size[r1] = root = r2;
            dsf->flip[r1] = f1 ^ f2 ^ inverse;
            f1 ^= dsf->flip[r1];
        }
        dsf->parent_or_size[root] = (s1 + s2) | DSF_FLAG_CANONICAL;

        if (dsf->min) {
            /* Update the min of the merged class */
            unsigned m1 = dsf->min[r1], m2 = dsf->min[r2];
            dsf->min[root] = m1 < m2 ? m1 : m2;
        }
    }

    /* Path-compress both paths from n1 and n2 so they point at the new root */
    dsf_path_compress_flip(dsf, n1, root, f1);
    dsf_path_compress_flip(dsf, n2, root, f2);
}

int dsf_minimal(DSF *dsf, int n)
{
    size_t root;

    assert(dsf->min && "dsf_minimal on a non-min dsf");

    root = dsf_canonify(dsf, n);
    return dsf->min[root];
}
