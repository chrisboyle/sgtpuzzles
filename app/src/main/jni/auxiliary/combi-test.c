#include <stdio.h>
#include "puzzles.h"

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
