#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "puzzles.h"

int main(int argc, char **argv)
{
    DSF *dsf;
    int i;
    int w = 9, h = 4, k = 6, tries = 100;
    random_state *rs;
    int fail_counter = 0;

    rs = random_new("123456", 6);

    if (argc > 1)
	w = atoi(argv[1]);
    if (argc > 2)
	h = atoi(argv[2]);
    if (argc > 3)
	k = atoi(argv[3]);
    if (argc > 4)
	tries = atoi(argv[4]);

    for (i = 0; i < tries; i++) {
	int x, y;

        while ((dsf = divvy_rectangle_attempt(w, h, k, rs)) == NULL)
            fail_counter++;

	for (y = 0; y <= 2*h; y++) {
	    for (x = 0; x <= 2*w; x++) {
		int miny = y/2 - 1 /*, maxy = y/2 */;
		int minx = x/2 - 1 /*, maxx = x/2 */;
		int classes[4], tx, ty;
		for (ty = 0; ty < 2; ty++)
		    for (tx = 0; tx < 2; tx++) {
			int cx = minx+tx, cy = miny+ty;
			if (cx < 0 || cx >= w || cy < 0 || cy >= h)
			    classes[ty*2+tx] = -1;
			else
			    classes[ty*2+tx] = dsf_canonify(dsf, cy*w+cx);
		    }
		switch (y%2 * 2 + x%2) {
		  case 0:	       /* corner */
		    /*
		     * Cases for the corner:
		     *
		     * 	- if all four surrounding squares belong
		     * 	  to the same omino, we print a space.
		     *
		     * 	- if the top two are the same and the
		     * 	  bottom two are the same, we print a
		     * 	  horizontal line.
		     *
		     * 	- if the left two are the same and the
		     * 	  right two are the same, we print a
		     * 	  vertical line.
		     *
		     *  - otherwise, we print a cross.
		     */
		    if (classes[0] == classes[1] &&
			classes[1] == classes[2] &&
			classes[2] == classes[3])
			printf(" ");
		    else if (classes[0] == classes[1] &&
			     classes[2] == classes[3])
			printf("-");
		    else if (classes[0] == classes[2] &&
			     classes[1] == classes[3])
			printf("|");
		    else
			printf("+");
		    break;
		  case 1:	       /* horiz edge */
		    if (classes[1] == classes[3])
			printf("  ");
		    else
			printf("--");
		    break;
		  case 2:	       /* vert edge */
		    if (classes[2] == classes[3])
			printf(" ");
		    else
			printf("|");
		    break;
		  case 3:	       /* square centre */
		    printf("  ");
		    break;
		}
	    }
	    printf("\n");
	}
	printf("\n");
	dsf_free(dsf);
    }

    printf("%d retries needed for %d successes\n", fail_counter, tries);

    return 0;
}
