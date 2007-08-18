/*
 * Library code to divide up a rectangle into a number of equally
 * sized ominoes, in a random fashion.
 * 
 * Could use this for generating solved grids of
 * http://www.nikoli.co.jp/ja/puzzles/block_puzzle/
 * or for generating the playfield for Jigsaw Sudoku.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "puzzles.h"

/*
 * Subroutine which implements a function used in computing both
 * whether a square can safely be added to an omino, and whether
 * it can safely be removed.
 * 
 * We enumerate the eight squares 8-adjacent to this one, in
 * cyclic order. We go round that loop and count the number of
 * times we find a square owned by the target omino next to one
 * not owned by it. We then return success iff that count is 2.
 * 
 * When adding a square to an omino, this is precisely the
 * criterion which tells us that adding the square won't leave a
 * hole in the middle of the omino. (There's no explicit
 * requirement in the statement of our problem that the ominoes be
 * simply connected, but we do know they must be all of equal size
 * and so it's clear that we must avoid leaving holes, since a
 * hole would necessarily be smaller than the maximum omino size.)
 * 
 * When removing a square from an omino, the _same_ criterion
 * tells us that removing the square won't disconnect the omino.
 */
static int addremcommon(int w, int h, int x, int y, int *own, int val)
{
    int neighbours[8];
    int dir, count;

    for (dir = 0; dir < 8; dir++) {
	int dx = ((dir & 3) == 2 ? 0 : dir > 2 && dir < 6 ? +1 : -1);
	int dy = ((dir & 3) == 0 ? 0 : dir < 4 ? -1 : +1);
	int sx = x+dx, sy = y+dy;

	if (sx < 0 || sx >= w || sy < 0 || sy >= h)
	    neighbours[dir] = -1;      /* outside the grid */
	else
	    neighbours[dir] = own[sy*w+sx];
    }

    /*
     * To begin with, check 4-adjacency.
     */
    if (neighbours[0] != val && neighbours[2] != val &&
	neighbours[4] != val && neighbours[6] != val)
	return FALSE;

    count = 0;

    for (dir = 0; dir < 8; dir++) {
	int next = (dir + 1) & 7;
	int gotthis = (neighbours[dir] == val);
	int gotnext = (neighbours[next] == val);

	if (gotthis != gotnext)
	    count++;
    }

    return (count == 2);
}

/*
 * w and h are the dimensions of the rectangle.
 * 
 * k is the size of the required ominoes. (So k must divide w*h,
 * of course.)
 * 
 * The returned result is a w*h-sized dsf.
 * 
 * In both of the above suggested use cases, the user would
 * probably want w==h==k, but that isn't a requirement.
 */
int *divvy_rectangle(int w, int h, int k, random_state *rs)
{
    int *order, *queue, *tmp, *own, *sizes, *addable, *removable, *retdsf;
    int wh = w*h;
    int i, j, n, x, y, qhead, qtail;

    n = wh / k;
    assert(wh == k*n);

    order = snewn(wh, int);
    tmp = snewn(wh, int);
    own = snewn(wh, int);
    sizes = snewn(n, int);
    queue = snewn(n, int);
    addable = snewn(wh*4, int);
    removable = snewn(wh, int);

    /*
     * Permute the grid squares into a random order, which will be
     * used for iterating over the grid whenever we need to search
     * for something. This prevents directional bias and arranges
     * for the answer to be non-deterministic.
     */
    for (i = 0; i < wh; i++)
	order[i] = i;
    shuffle(order, wh, sizeof(*order), rs);

    /*
     * Begin by choosing a starting square at random for each
     * omino.
     */
    for (i = 0; i < wh; i++) {
	own[i] = -1;
    }
    for (i = 0; i < n; i++) {
	own[order[i]] = i;
	sizes[i] = 1;
    }

    /*
     * Now repeatedly pick a random omino which isn't already at
     * the target size, and find a way to expand it by one. This
     * may involve stealing a square from another omino, in which
     * case we then re-expand that omino, forming a chain of
     * square-stealing which terminates in an as yet unclaimed
     * square. Hence every successful iteration around this loop
     * causes the number of unclaimed squares to drop by one, and
     * so the process is bounded in duration.
     */
    while (1) {

#ifdef DIVVY_DIAGNOSTICS
	{
	    int x, y;
	    printf("Top of loop. Current grid:\n");
	    for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++)
		    printf("%3d", own[y*w+x]);
		printf("\n");
	    }
	}
#endif

	/*
	 * Go over the grid and figure out which squares can
	 * safely be added to, or removed from, each omino. We
	 * don't take account of other ominoes in this process, so
	 * we will often end up knowing that a square can be
	 * poached from one omino by another.
	 * 
	 * For each square, there may be up to four ominoes to
	 * which it can be added (those to which it is
	 * 4-adjacent).
	 */
	for (y = 0; y < h; y++) {
	    for (x = 0; x < w; x++) {
		int yx = y*w+x;
		int curr = own[yx];
		int dir;

		if (curr < 0) {
		    removable[yx] = 0; /* can't remove if it's not owned! */
		} else {
		    /*
		     * See if this square can be removed from its
		     * omino without disconnecting it.
		     */
		    removable[yx] = addremcommon(w, h, x, y, own, curr);
		}

		for (dir = 0; dir < 4; dir++) {
		    int dx = (dir == 0 ? -1 : dir == 1 ? +1 : 0);
		    int dy = (dir == 2 ? -1 : dir == 3 ? +1 : 0);
		    int sx = x + dx, sy = y + dy;
		    int syx = sy*w+sx;

		    addable[yx*4+dir] = -1;

		    if (sx < 0 || sx >= w || sy < 0 || sy >= h)
			continue;      /* no omino here! */
		    if (own[syx] < 0)
			continue;      /* also no omino here */
		    if (own[syx] == own[yx])
			continue;      /* we already got one */
		    if (!addremcommon(w, h, x, y, own, own[syx]))
			continue;      /* would non-simply connect the omino */
		    
		    addable[yx*4+dir] = own[syx];
		}
	    }
	}

	for (i = j = 0; i < n; i++)
	    if (sizes[i] < k)
		tmp[j++] = i;
	if (j == 0)
	    break;		       /* all ominoes are complete! */
	j = tmp[random_upto(rs, j)];

	/*
	 * So we're trying to expand omino j. We breadth-first
	 * search out from j across the space of ominoes.
	 * 
	 * For bfs purposes, we use two elements of tmp per omino:
	 * tmp[2*i+0] tells us which omino we got to i from, and
	 * tmp[2*i+1] numbers the grid square that omino stole
	 * from us.
	 * 
	 * This requires that wh (the size of tmp) is at least 2n,
	 * i.e. k is at least 2. There would have been nothing to
	 * stop a user calling this function with k=1, but if they
	 * did then we wouldn't have got to _here_ in the code -
	 * we would have noticed above that all ominoes were
	 * already at their target sizes, and terminated :-)
	 */
	assert(wh >= 2*n);
	for (i = 0; i < n; i++)
	    tmp[2*i] = tmp[2*i+1] = -1;
	qhead = qtail = 0;
	queue[qtail++] = j;
	tmp[2*j] = tmp[2*j+1] = -2;    /* special value: `starting point' */

	while (qhead < qtail) {
	    int tmpsq;

	    j = queue[qhead];

	    /*
	     * We wish to expand omino j. However, we might have
	     * got here by omino j having a square stolen from it,
	     * so first of all we must temporarily mark that
	     * square as not belonging to j, so that our adjacency
	     * calculations don't assume j _does_ belong to us.
	     */
	    tmpsq = tmp[2*j+1];
	    if (tmpsq >= 0) {
		assert(own[tmpsq] == j);
		own[tmpsq] = -1;
	    }

	    /*
	     * OK. Now begin by seeing if we can find any
	     * unclaimed square into which we can expand omino j.
	     * If we find one, the entire bfs terminates.
	     */
	    for (i = 0; i < wh; i++) {
		int dir;

		if (own[order[i]] >= 0)
		    continue;	       /* this square is claimed */
		for (dir = 0; dir < 4; dir++)
		    if (addable[order[i]*4+dir] == j) {
			/*
			 * We know this square is addable to this
			 * omino with the grid in the state it had
			 * at the top of the loop. However, we
			 * must now check that it's _still_
			 * addable to this omino when the omino is
			 * missing a square. To do this it's only
			 * necessary to re-check addremcommon.
~|~			 */
			if (!addremcommon(w, h, order[i]%w, order[i]/w,
					  own, j))
			    continue;
			break;
		    }
		if (dir == 4)
		    continue;	       /* we can't add this square to j */
		break;		       /* got one! */
	    }
	    if (i < wh) {
		i = order[i];

		/*
		 * We are done. We can add square i to omino j,
		 * and then backtrack along the trail in tmp
		 * moving squares between ominoes, ending up
		 * expanding our starting omino by one.
		 */
		while (1) {
		    own[i] = j;
		    if (tmp[2*j] == -2)
			break;
		    i = tmp[2*j+1];
		    j = tmp[2*j];
		}

		/*
		 * Increment the size of the starting omino.
		 */
		sizes[j]++;

		/*
		 * Terminate the bfs loop.
		 */
		break;
	    }

	    /*
	     * If we get here, we haven't been able to expand
	     * omino j into an unclaimed square. So now we begin
	     * to investigate expanding it into squares which are
	     * claimed by ominoes the bfs has not yet visited.
	     */
	    for (i = 0; i < wh; i++) {
		int dir, nj;

		nj = own[order[i]];
		if (nj < 0 || tmp[2*nj] != -1)
		    continue;	       /* unclaimed, or owned by wrong omino */
		if (!removable[order[i]])
		    continue;	       /* its omino won't let it go */

		for (dir = 0; dir < 4; dir++)
		    if (addable[order[i]*4+dir] == j) {
			/*
			 * As above, re-check addremcommon.
			 */
			if (!addremcommon(w, h, order[i]%w, order[i]/w,
					  own, j))
			    continue;

			/*
			 * We have found a square we can use to
			 * expand omino j, at the expense of the
			 * as-yet unvisited omino nj. So add this
			 * to the bfs queue.
			 */
			assert(qtail < n);
			queue[qtail++] = nj;
			tmp[2*nj] = j;
			tmp[2*nj+1] = order[i];

			/*
			 * Now terminate the loop over dir, to
			 * ensure we don't accidentally add the
			 * same omino twice to the queue.
			 */
			break;
		    }
	    }

	    /*
	     * Restore the temporarily removed square.
	     */
	    if (tmpsq >= 0)
		own[tmpsq] = j;

	    /*
	     * Advance the queue head.
	     */
	    qhead++;
	}

	if (qhead == qtail) {
	    /*
	     * We have finished the bfs and not found any way to
	     * expand omino j. Panic, and return failure.
	     * 
	     * FIXME: or should we loop over all ominoes before we
	     * give up?
	     */
	    retdsf = NULL;
	    goto cleanup;
	}
    }

    /*
     * Construct the output dsf.
     */
    for (i = 0; i < wh; i++) {
	assert(own[i] >= 0 && own[i] < n);
	tmp[own[i]] = i;
    }
    retdsf = snew_dsf(wh);
    for (i = 0; i < wh; i++) {
	dsf_merge(retdsf, i, tmp[own[i]]);
    }

    /*
     * Construct the output dsf a different way, to verify that
     * the ominoes really are k-ominoes and we haven't
     * accidentally split one into two disconnected pieces.
     */
    dsf_init(tmp, wh);
    for (y = 0; y < h; y++)
	for (x = 0; x+1 < w; x++)
	    if (own[y*w+x] == own[y*w+(x+1)])
		dsf_merge(tmp, y*w+x, y*w+(x+1));
    for (x = 0; x < w; x++)
	for (y = 0; y+1 < h; y++)
	    if (own[y*w+x] == own[(y+1)*w+x])
		dsf_merge(tmp, y*w+x, (y+1)*w+x);
    for (i = 0; i < wh; i++) {
	j = dsf_canonify(retdsf, i);
	assert(dsf_canonify(tmp, j) == dsf_canonify(tmp, i));
    }

    cleanup:

    /*
     * Free our temporary working space.
     */
    sfree(order);
    sfree(tmp);
    sfree(own);
    sfree(sizes);
    sfree(queue);
    sfree(addable);
    sfree(removable);

    /*
     * And we're done.
     */
    return retdsf;
}

#ifdef TESTMODE

/*
 * gcc -g -O0 -DTESTMODE -I.. -o divvy divvy.c ../random.c ../malloc.c ../dsf.c ../misc.c ../nullfe.c
 * 
 * or to debug
 * 
 * gcc -g -O0 -DDIVVY_DIAGNOSTICS -DTESTMODE -I.. -o divvy divvy.c ../random.c ../malloc.c ../dsf.c ../misc.c ../nullfe.c
 */

int main(int argc, char **argv)
{
    int *dsf;
    int i, successes;
    int w = 9, h = 4, k = 6, tries = 100;
    random_state *rs;

    rs = random_new("123456", 6);

    if (argc > 1)
	w = atoi(argv[1]);
    if (argc > 2)
	h = atoi(argv[2]);
    if (argc > 3)
	k = atoi(argv[3]);
    if (argc > 4)
	tries = atoi(argv[4]);

    successes = 0;
    for (i = 0; i < tries; i++) {
	dsf = divvy_rectangle(w, h, k, rs);
	if (dsf) {
	    int x, y;

	    successes++;

	    for (y = 0; y <= 2*h; y++) {
		for (x = 0; x <= 2*w; x++) {
		    int miny = y/2 - 1, maxy = y/2;
		    int minx = x/2 - 1, maxx = x/2;
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
			 *  - if all four surrounding squares
			 *    belong to the same omino, we print a
			 *    space.
			 * 
			 *  - if the top two are the same and the
			 *    bottom two are the same, we print a
			 *    horizontal line.
			 * 
			 *  - if the left two are the same and the
			 *    right two are the same, we print a
			 *    vertical line.
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
	    sfree(dsf);
	}
    }

    printf("%d successes out of %d tries\n", successes, tries);

    return 0;
}

#endif
