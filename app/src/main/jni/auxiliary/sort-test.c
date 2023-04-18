#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "puzzles.h"

static int testcmp(const void *av, const void *bv, void *ctx)
{
    int a = *(const int *)av, b = *(const int *)bv;
    const int *keys = (const int *)ctx;
    return keys[a] < keys[b] ? -1 : keys[a] > keys[b] ? +1 : 0;
}

static int resetcmp(const void *av, const void *bv)
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
