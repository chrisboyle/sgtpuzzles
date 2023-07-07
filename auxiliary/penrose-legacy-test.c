#include <stdio.h>
#include <string.h>

#include "puzzles.h"
#include "penrose-legacy.h"

static int show_recursion = 0;
static int ntiles, nfinal;

static int test_cb(penrose_legacy_state *state, vector *vs, int n, int depth)
{
    int i, xoff = 0, yoff = 0;
    double l = penrose_legacy_side_length(state->start_size, depth);
    double rball = l / 10.0;
    const char *col;

    ntiles++;
    if (state->max_depth == depth) {
        col = n == 4 ? "black" : "green";
        nfinal++;
    } else {
        if (!show_recursion)
            return 0;
        col = n == 4 ? "red" : "blue";
    }
    if (n != 4) yoff = state->start_size;

    printf("<polygon points=\"");
    for (i = 0; i < n; i++) {
        printf("%s%f,%f", (i == 0) ? "" : " ",
               penrose_legacy_vx(vs, i) + xoff,
               penrose_legacy_vy(vs, i) + yoff);
    }
    printf("\" style=\"fill: %s; fill-opacity: 0.2; stroke: %s\" />\n", col, col);
    printf("<ellipse cx=\"%f\" cy=\"%f\" rx=\"%f\" ry=\"%f\" fill=\"%s\" />",
           penrose_legacy_vx(vs, 0) + xoff, penrose_legacy_vy(vs, 0) + yoff,
           rball, rball, col);

    return 0;
}

static void usage_exit(void)
{
    fprintf(stderr, "Usage: penrose-legacy-test [--recursion] "
            "P2|P3 SIZE DEPTH\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    penrose_legacy_state ps;
    int which = 0;

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "-h") || !strcmp(p, "--help")) {
            usage_exit();
        } else if (!strcmp(p, "--recursion")) {
            show_recursion = 1;
        } else if (*p == '-') {
            fprintf(stderr, "Unrecognised option '%s'\n", p);
            exit(1);
        } else {
            break;
        }
    }

    if (argc < 3) usage_exit();

    if (strcmp(argv[0], "P2") == 0) which = PENROSE_P2;
    else if (strcmp(argv[0], "P3") == 0) which = PENROSE_P3;
    else usage_exit();

    ps.start_size = atoi(argv[1]);
    ps.max_depth = atoi(argv[2]);
    ps.new_tile = test_cb;

    ntiles = nfinal = 0;

    printf("\
<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n\
<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 20010904//EN\"\n\
\"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n\
\n\
<svg xmlns=\"http://www.w3.org/2000/svg\"\n\
xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n\n");

    printf("<g>\n");
    penrose_legacy(&ps, which, 0);
    printf("</g>\n");

    printf("<!-- %d tiles and %d leaf tiles total -->\n",
           ntiles, nfinal);

    printf("</svg>");

    return 0;
}
