/*
 * tdq.c: implement a 'to-do queue', a simple de-duplicating to-do
 * list mechanism.
 */

#include <assert.h>

#include "puzzles.h"

/*
 * Implementation: a tdq consists of a circular buffer of size n
 * storing the integers currently in the queue, plus an array of n
 * booleans indicating whether each integer is already there.
 *
 * Using a circular buffer of size n to store between 0 and n items
 * inclusive has an obvious failure mode: if the input and output
 * pointers are the same, how do you know whether that means the
 * buffer is full or empty?
 *
 * In this application we have a simple way to tell: in the former
 * case, the flags array is all 1s, and in the latter case it's all
 * 0s. So we could spot that case and check, say, flags[0].
 *
 * However, it's even easier to simply determine whether the queue is
 * non-empty by testing flags[buffer[op]] - that way we don't even
 * _have_ to compare ip against op.
 */

struct tdq {
    int n;
    int *queue;
    int ip, op;                        /* in pointer, out pointer */
    bool *flags;
};

tdq *tdq_new(int n)
{
    int i;
    tdq *tdq = snew(struct tdq);
    tdq->queue = snewn(n, int);
    tdq->flags = snewn(n, bool);
    for (i = 0; i < n; i++) {
        tdq->queue[i] = 0;
        tdq->flags[i] = false;
    }
    tdq->n = n;
    tdq->ip = tdq->op = 0;
    return tdq;
}

void tdq_free(tdq *tdq)
{
    sfree(tdq->queue);
    sfree(tdq->flags);
    sfree(tdq);
}

void tdq_add(tdq *tdq, int k)
{
    assert((unsigned)k < (unsigned)tdq->n);
    if (!tdq->flags[k]) {
        tdq->queue[tdq->ip] = k;
        tdq->flags[k] = true;
        if (++tdq->ip == tdq->n)
            tdq->ip = 0;
    }
}

int tdq_remove(tdq *tdq)
{
    int ret = tdq->queue[tdq->op];

    if (!tdq->flags[ret])
        return -1;

    tdq->flags[ret] = false;
    if (++tdq->op == tdq->n)
        tdq->op = 0;

    return ret;
}

void tdq_fill(tdq *tdq)
{
    int i;
    for (i = 0; i < tdq->n; i++)
        tdq_add(tdq, i);
}
