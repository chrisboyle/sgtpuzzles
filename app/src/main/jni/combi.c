#include <assert.h>
#include <string.h>

#include "puzzles.h"

/* horrific and doesn't check overflow. */
static long factx(long x, long y)
{
    long acc = 1, i;

    for (i = y; i <= x; i++)
        acc *= i;
    return acc;
}

void reset_combi(combi_ctx *combi)
{
    int i;
    combi->nleft = combi->total;
    for (i = 0; i < combi->r; i++)
        combi->a[i] = i;
}

combi_ctx *new_combi(int r, int n)
{
    long nfr, nrf;
    combi_ctx *combi;

    assert(r <= n);
    assert(n >= 1);

    combi = snew(combi_ctx);
    memset(combi, 0, sizeof(combi_ctx));
    combi->r = r;
    combi->n = n;

    combi->a = snewn(r, int);
    memset(combi->a, 0, r * sizeof(int));

    nfr = factx(n, r+1);
    nrf = factx(n-r, 1);
    combi->total = (int)(nfr / nrf);

    reset_combi(combi);
    return combi;
}

/* returns NULL when we're done otherwise returns input. */
combi_ctx *next_combi(combi_ctx *combi)
{
    int i = combi->r - 1, j;

    if (combi->nleft == combi->total)
        goto done;
    else if (combi->nleft <= 0)
        return NULL;

    while (combi->a[i] == combi->n - combi->r + i)
        i--;
    combi->a[i] += 1;
    for (j = i+1; j < combi->r; j++)
        combi->a[j] = combi->a[i] + j - i;

    done:
    combi->nleft--;
    return combi;
}

void free_combi(combi_ctx *combi)
{
    sfree(combi->a);
    sfree(combi);
}

/* compile this with:
 *   gcc -o combi.exe -DSTANDALONE_COMBI_TEST combi.c malloc.c
 */
#ifdef STANDALONE_COMBI_TEST

#include <stdio.h>

int main(int argc, char *argv[])
{
    combi_ctx *c;
    int i, r, n;

    if (argc < 3) {
        fprintf(stderr, "Usage: combi R N\n");
        exit(1);
    }

    r = atoi(argv[1]); n = atoi(argv[2]);
    c = new_combi(r, n);
    printf("combi %d of %d, %d elements.\n", c->r, c->n, c->total);

    while (next_combi(c)) {
        for (i = 0; i < c->r; i++) {
            printf("%d ", c->a[i]);
        }
        printf("\n");
    }
    free_combi(c);
}

#endif
