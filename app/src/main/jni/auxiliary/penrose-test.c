#include <stdio.h>
#include <math.h>

#include "puzzles.h"
#include "penrose.h"
#include "penrose-internal.h"

struct testctx {
    double sqrt5, xunit, yunit;
};

static void tile(void *vctx, const int *coords)
{
    struct testctx *tctx = (struct testctx *)vctx;
    size_t i;

    printf("newpath");
    for (i = 0; i < 4; i++) {
        printf(" %g %g %s",
               tctx->xunit * (coords[4*i+0] + tctx->sqrt5 * coords[4*i+1]),
               tctx->yunit * (coords[4*i+2] + tctx->sqrt5 * coords[4*i+3]),
               i == 0 ? "moveto" : "lineto");
    }
    printf(" closepath gsave 0.7 setgray fill grestore stroke\n");
}

int main(void)
{
    random_state *rs;
    struct PenrosePatchParams params[1];
    struct testctx tctx[1];
    int w = 50, h = 40;

    tctx->sqrt5 = sqrt(5);
    tctx->xunit = 25 * 0.25;
    tctx->yunit = 25 * sin(atan2(0, -1)/5) / 2;
    printf("newpath 0 0 moveto %g 0 rlineto 0 %g rlineto %g 0 rlineto "
           "closepath stroke\n", w * tctx->xunit, h * tctx->yunit,
           -w * tctx->xunit);

    rs = random_new("12345", 5);
    penrose_tiling_randomise(params, PENROSE_P2, w, h, rs);
    penrose_tiling_generate(params, w, h, tile, tctx);
    sfree(params->coords);
    random_free(rs);
}
