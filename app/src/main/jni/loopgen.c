/*
 * loopgen.c: loop generation functions for grid.[ch].
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"
#include "tree234.h"
#include "grid.h"
#include "loopgen.h"


/* We're going to store lists of current candidate faces for colouring black
 * or white.
 * Each face gets a 'score', which tells us how adding that face right
 * now would affect the curliness of the solution loop.  We're trying to
 * maximise that quantity so will bias our random selection of faces to
 * colour those with high scores */
struct face_score {
    int white_score;
    int black_score;
    unsigned long random;
    /* No need to store a grid_face* here.  The 'face_scores' array will
     * be a list of 'face_score' objects, one for each face of the grid, so
     * the position (index) within the 'face_scores' array will determine
     * which face corresponds to a particular face_score.
     * Having a single 'face_scores' array for all faces simplifies memory
     * management, and probably improves performance, because we don't have to 
     * malloc/free each individual face_score, and we don't have to maintain
     * a mapping from grid_face* pointers to face_score* pointers.
     */
};

static int generic_sort_cmpfn(void *v1, void *v2, size_t offset)
{
    struct face_score *f1 = v1;
    struct face_score *f2 = v2;
    int r;

    r = *(int *)((char *)f2 + offset) - *(int *)((char *)f1 + offset);
    if (r) {
        return r;
    }

    if (f1->random < f2->random)
        return -1;
    else if (f1->random > f2->random)
        return 1;

    /*
     * It's _just_ possible that two faces might have been given
     * the same random value. In that situation, fall back to
     * comparing based on the positions within the face_scores list.
     * This introduces a tiny directional bias, but not a significant one.
     */
    return f1 - f2;
}

static int white_sort_cmpfn(void *v1, void *v2)
{
    return generic_sort_cmpfn(v1, v2, offsetof(struct face_score,white_score));
}

static int black_sort_cmpfn(void *v1, void *v2)
{
    return generic_sort_cmpfn(v1, v2, offsetof(struct face_score,black_score));
}

/* 'board' is an array of enum face_colour, indicating which faces are
 * currently black/white/grey.  'colour' is FACE_WHITE or FACE_BLACK.
 * Returns whether it's legal to colour the given face with this colour. */
static bool can_colour_face(grid *g, char* board, int face_index,
                            enum face_colour colour)
{
    int i, j;
    grid_face *test_face = g->faces + face_index;
    grid_face *starting_face, *current_face;
    grid_dot *starting_dot;
    int transitions;
    bool current_state, s; /* equal or not-equal to 'colour' */
    bool found_same_coloured_neighbour = false;
    assert(board[face_index] != colour);

    /* Can only consider a face for colouring if it's adjacent to a face
     * with the same colour. */
    for (i = 0; i < test_face->order; i++) {
        grid_edge *e = test_face->edges[i];
        grid_face *f = (e->face1 == test_face) ? e->face2 : e->face1;
        if (FACE_COLOUR(f) == colour) {
            found_same_coloured_neighbour = true;
            break;
        }
    }
    if (!found_same_coloured_neighbour)
        return false;

    /* Need to avoid creating a loop of faces of this colour around some
     * differently-coloured faces.
     * Also need to avoid meeting a same-coloured face at a corner, with
     * other-coloured faces in between.  Here's a simple test that (I believe)
     * takes care of both these conditions:
     *
     * Take the circular path formed by this face's edges, and inflate it
     * slightly outwards.  Imagine walking around this path and consider
     * the faces that you visit in sequence.  This will include all faces
     * touching the given face, either along an edge or just at a corner.
     * Count the number of 'colour'/not-'colour' transitions you encounter, as
     * you walk along the complete loop.  This will obviously turn out to be
     * an even number.
     * If 0, we're either in the middle of an "island" of this colour (should
     * be impossible as we're not supposed to create black or white loops),
     * or we're about to start a new island - also not allowed.
     * If 4 or greater, there are too many separate coloured regions touching
     * this face, and colouring it would create a loop or a corner-violation.
     * The only allowed case is when the count is exactly 2. */

    /* i points to a dot around the test face.
     * j points to a face around the i^th dot.
     * The current face will always be:
     *     test_face->dots[i]->faces[j]
     * We assume dots go clockwise around the test face,
     * and faces go clockwise around dots. */

    /*
     * The end condition is slightly fiddly. In sufficiently strange
     * degenerate grids, our test face may be adjacent to the same
     * other face multiple times (typically if it's the exterior
     * face). Consider this, in particular:
     * 
     *   +--+
     *   |  |
     *   +--+--+
     *   |  |  |
     *   +--+--+
     * 
     * The bottom left face there is adjacent to the exterior face
     * twice, so we can't just terminate our iteration when we reach
     * the same _face_ we started at. Furthermore, we can't
     * condition on having the same (i,j) pair either, because
     * several (i,j) pairs identify the bottom left contiguity with
     * the exterior face! We canonicalise the (i,j) pair by taking
     * one step around before we set the termination tracking.
     */

    i = j = 0;
    current_face = test_face->dots[0]->faces[0];
    if (current_face == test_face) {
        j = 1;
        current_face = test_face->dots[0]->faces[1];
    }
    transitions = 0;
    current_state = (FACE_COLOUR(current_face) == colour);
    starting_dot = NULL;
    starting_face = NULL;
    while (true) {
        /* Advance to next face.
         * Need to loop here because it might take several goes to
         * find it. */
        while (true) {
            j++;
            if (j == test_face->dots[i]->order)
                j = 0;

            if (test_face->dots[i]->faces[j] == test_face) {
                /* Advance to next dot round test_face, then
                 * find current_face around new dot
                 * and advance to the next face clockwise */
                i++;
                if (i == test_face->order)
                    i = 0;
                for (j = 0; j < test_face->dots[i]->order; j++) {
                    if (test_face->dots[i]->faces[j] == current_face)
                        break;
                }
                /* Must actually find current_face around new dot,
                 * or else something's wrong with the grid. */
                assert(j != test_face->dots[i]->order);
                /* Found, so advance to next face and try again */
            } else {
                break;
            }
        }
        /* (i,j) are now advanced to next face */
        current_face = test_face->dots[i]->faces[j];
        s = (FACE_COLOUR(current_face) == colour);
	if (!starting_dot) {
	    starting_dot = test_face->dots[i];
	    starting_face = current_face;
	    current_state = s;
	} else {
	    if (s != current_state) {
		++transitions;
		current_state = s;
		if (transitions > 2)
		    break;
	    }
	    if (test_face->dots[i] == starting_dot &&
		current_face == starting_face)
		break;
        }
    }

    return (transitions == 2) ? true : false;
}

/* Count the number of neighbours of 'face', having colour 'colour' */
static int face_num_neighbours(grid *g, char *board, grid_face *face,
                               enum face_colour colour)
{
    int colour_count = 0;
    int i;
    grid_face *f;
    grid_edge *e;
    for (i = 0; i < face->order; i++) {
        e = face->edges[i];
        f = (e->face1 == face) ? e->face2 : e->face1;
        if (FACE_COLOUR(f) == colour)
            ++colour_count;
    }
    return colour_count;
}

/* The 'score' of a face reflects its current desirability for selection
 * as the next face to colour white or black.  We want to encourage moving
 * into grey areas and increasing loopiness, so we give scores according to
 * how many of the face's neighbours are currently coloured the same as the
 * proposed colour. */
static int face_score(grid *g, char *board, grid_face *face,
                      enum face_colour colour)
{
    /* Simple formula: score = 0 - num. same-coloured neighbours,
     * so a higher score means fewer same-coloured neighbours. */
    return -face_num_neighbours(g, board, face, colour);
}

/*
 * Generate a new complete random closed loop for the given grid.
 *
 * The method is to generate a WHITE/BLACK colouring of all the faces,
 * such that the WHITE faces will define the inside of the path, and the
 * BLACK faces define the outside.
 * To do this, we initially colour all faces GREY.  The infinite space outside
 * the grid is coloured BLACK, and we choose a random face to colour WHITE.
 * Then we gradually grow the BLACK and the WHITE regions, eliminating GREY
 * faces, until the grid is filled with BLACK/WHITE.  As we grow the regions,
 * we avoid creating loops of a single colour, to preserve the topological
 * shape of the WHITE and BLACK regions.
 * We also try to make the boundary as loopy and twisty as possible, to avoid
 * generating paths that are uninteresting.
 * The algorithm works by choosing a BLACK/WHITE colour, then choosing a GREY
 * face that can be coloured with that colour (without violating the
 * topological shape of that region).  It's not obvious, but I think this
 * algorithm is guaranteed to terminate without leaving any GREY faces behind.
 * Indeed, if there are any GREY faces at all, both the WHITE and BLACK
 * regions can be grown.
 * This is checked using assert()ions, and I haven't seen any failures yet.
 *
 * Hand-wavy proof: imagine what can go wrong...
 *
 * Could the white faces get completely cut off by the black faces, and still
 * leave some grey faces remaining?
 * No, because then the black faces would form a loop around both the white
 * faces and the grey faces, which is disallowed because we continually
 * maintain the correct topological shape of the black region.
 * Similarly, the black faces can never get cut off by the white faces.  That
 * means both the WHITE and BLACK regions always have some room to grow into
 * the GREY regions.
 * Could it be that we can't colour some GREY face, because there are too many
 * WHITE/BLACK transitions as we walk round the face? (see the
 * can_colour_face() function for details)
 * No.  Imagine otherwise, and we see WHITE/BLACK/WHITE/BLACK as we walk
 * around the face.  The two WHITE faces would be connected by a WHITE path,
 * and the BLACK faces would be connected by a BLACK path.  These paths would
 * have to cross, which is impossible.
 * Another thing that could go wrong: perhaps we can't find any GREY face to
 * colour WHITE, because it would create a loop-violation or a corner-violation
 * with the other WHITE faces?
 * This is a little bit tricky to prove impossible.  Imagine you have such a
 * GREY face (that is, if you coloured it WHITE, you would create a WHITE loop
 * or corner violation).
 * That would cut all the non-white area into two blobs.  One of those blobs
 * must be free of BLACK faces (because the BLACK stuff is a connected blob).
 * So we have a connected GREY area, completely surrounded by WHITE
 * (including the GREY face we've tentatively coloured WHITE).
 * A well-known result in graph theory says that you can always find a GREY
 * face whose removal leaves the remaining GREY area connected.  And it says
 * there are at least two such faces, so we can always choose the one that
 * isn't the "tentative" GREY face.  Colouring that face WHITE leaves
 * everything nice and connected, including that "tentative" GREY face which
 * acts as a gateway to the rest of the non-WHITE grid.
 */
void generate_loop(grid *g, char *board, random_state *rs,
                   loopgen_bias_fn_t bias, void *biasctx)
{
    int i, j;
    int num_faces = g->num_faces;
    struct face_score *face_scores; /* Array of face_score objects */
    struct face_score *fs; /* Points somewhere in the above list */
    struct grid_face *cur_face;
    tree234 *lightable_faces_sorted;
    tree234 *darkable_faces_sorted;
    int *face_list;
    bool do_random_pass;

    /* Make a board */
    memset(board, FACE_GREY, num_faces);
    
    /* Create and initialise the list of face_scores */
    face_scores = snewn(num_faces, struct face_score);
    for (i = 0; i < num_faces; i++) {
        face_scores[i].random = random_bits(rs, 31);
        face_scores[i].black_score = face_scores[i].white_score = 0;
    }
    
    /* Colour a random, finite face white.  The infinite face is implicitly
     * coloured black.  Together, they will seed the random growth process
     * for the black and white areas. */
    i = random_upto(rs, num_faces);
    board[i] = FACE_WHITE;

    /* We need a way of favouring faces that will increase our loopiness.
     * We do this by maintaining a list of all candidate faces sorted by
     * their score and choose randomly from that with appropriate skew.
     * In order to avoid consistently biasing towards particular faces, we
     * need the sort order _within_ each group of scores to be completely
     * random.  But it would be abusing the hospitality of the tree234 data
     * structure if our comparison function were nondeterministic :-).  So with
     * each face we associate a random number that does not change during a
     * particular run of the generator, and use that as a secondary sort key.
     * Yes, this means we will be biased towards particular random faces in
     * any one run but that doesn't actually matter. */

    lightable_faces_sorted = newtree234(white_sort_cmpfn);
    darkable_faces_sorted = newtree234(black_sort_cmpfn);

    /* Initialise the lists of lightable and darkable faces.  This is
     * slightly different from the code inside the while-loop, because we need
     * to check every face of the board (the grid structure does not keep a
     * list of the infinite face's neighbours). */
    for (i = 0; i < num_faces; i++) {
        grid_face *f = g->faces + i;
        struct face_score *fs = face_scores + i;
        if (board[i] != FACE_GREY) continue;
        /* We need the full colourability check here, it's not enough simply
         * to check neighbourhood.  On some grids, a neighbour of the infinite
         * face is not necessarily darkable. */
        if (can_colour_face(g, board, i, FACE_BLACK)) {
            fs->black_score = face_score(g, board, f, FACE_BLACK);
            add234(darkable_faces_sorted, fs);
        }
        if (can_colour_face(g, board, i, FACE_WHITE)) {
            fs->white_score = face_score(g, board, f, FACE_WHITE);
            add234(lightable_faces_sorted, fs);
        }
    }

    /* Colour faces one at a time until no more faces are colourable. */
    while (true)
    {
        enum face_colour colour;
        tree234 *faces_to_pick;
        int c_lightable = count234(lightable_faces_sorted);
        int c_darkable = count234(darkable_faces_sorted);
        if (c_lightable == 0 && c_darkable == 0) {
            /* No more faces we can use at all. */
            break;
        }
	assert(c_lightable != 0 && c_darkable != 0);

        /* Choose a colour, and colour the best available face
         * with that colour. */
        colour = random_upto(rs, 2) ? FACE_WHITE : FACE_BLACK;

        if (colour == FACE_WHITE)
            faces_to_pick = lightable_faces_sorted;
        else
            faces_to_pick = darkable_faces_sorted;
        if (bias) {
            /*
             * Go through all the candidate faces and pick the one the
             * bias function likes best, breaking ties using the
             * ordering in our tree234 (which is why we replace only
             * if score > bestscore, not >=).
             */
            int j, k;
            struct face_score *best = NULL;
            int score, bestscore = 0;

            for (j = 0;
                 (fs = (struct face_score *)index234(faces_to_pick, j))!=NULL;
                 j++) {

                assert(fs);
                k = fs - face_scores;
                assert(board[k] == FACE_GREY);
                board[k] = colour;
                score = bias(biasctx, board, k);
                board[k] = FACE_GREY;
                bias(biasctx, board, k); /* let bias know we put it back */

                if (!best || score > bestscore) {
                    bestscore = score;
                    best = fs;
                }
            }
            fs = best;
        } else {
            fs = (struct face_score *)index234(faces_to_pick, 0);
        }
        assert(fs);
        i = fs - face_scores;
        assert(board[i] == FACE_GREY);
        board[i] = colour;
        if (bias)
            bias(biasctx, board, i); /* notify bias function of the change */

        /* Remove this newly-coloured face from the lists.  These lists should
         * only contain grey faces. */
        del234(lightable_faces_sorted, fs);
        del234(darkable_faces_sorted, fs);

        /* Remember which face we've just coloured */
        cur_face = g->faces + i;

        /* The face we've just coloured potentially affects the colourability
         * and the scores of any neighbouring faces (touching at a corner or
         * edge).  So the search needs to be conducted around all faces
         * touching the one we've just lit.  Iterate over its corners, then
         * over each corner's faces.  For each such face, we remove it from
         * the lists, recalculate any scores, then add it back to the lists
         * (depending on whether it is lightable, darkable or both). */
        for (i = 0; i < cur_face->order; i++) {
            grid_dot *d = cur_face->dots[i];
            for (j = 0; j < d->order; j++) {
                grid_face *f = d->faces[j];
                int fi; /* face index of f */

                if (f == NULL)
                    continue;
                if (f == cur_face)
                    continue;
                
                /* If the face is already coloured, it won't be on our
                 * lightable/darkable lists anyway, so we can skip it without 
                 * bothering with the removal step. */
                if (FACE_COLOUR(f) != FACE_GREY) continue; 

                /* Find the face index and face_score* corresponding to f */
                fi = f - g->faces;                
                fs = face_scores + fi;

                /* Remove from lightable list if it's in there.  We do this,
                 * even if it is still lightable, because the score might
                 * be different, and we need to remove-then-add to maintain
                 * correct sort order. */
                del234(lightable_faces_sorted, fs);
                if (can_colour_face(g, board, fi, FACE_WHITE)) {
                    fs->white_score = face_score(g, board, f, FACE_WHITE);
                    add234(lightable_faces_sorted, fs);
                }
                /* Do the same for darkable list. */
                del234(darkable_faces_sorted, fs);
                if (can_colour_face(g, board, fi, FACE_BLACK)) {
                    fs->black_score = face_score(g, board, f, FACE_BLACK);
                    add234(darkable_faces_sorted, fs);
                }
            }
        }
    }

    /* Clean up */
    freetree234(lightable_faces_sorted);
    freetree234(darkable_faces_sorted);
    sfree(face_scores);

    /* The next step requires a shuffled list of all faces */
    face_list = snewn(num_faces, int);
    for (i = 0; i < num_faces; ++i) {
        face_list[i] = i;
    }
    shuffle(face_list, num_faces, sizeof(int), rs);

    /* The above loop-generation algorithm can often leave large clumps
     * of faces of one colour.  In extreme cases, the resulting path can be 
     * degenerate and not very satisfying to solve.
     * This next step alleviates this problem:
     * Go through the shuffled list, and flip the colour of any face we can
     * legally flip, and which is adjacent to only one face of the opposite
     * colour - this tends to grow 'tendrils' into any clumps.
     * Repeat until we can find no more faces to flip.  This will
     * eventually terminate, because each flip increases the loop's
     * perimeter, which cannot increase for ever.
     * The resulting path will have maximal loopiness (in the sense that it
     * cannot be improved "locally".  Unfortunately, this allows a player to
     * make some illicit deductions.  To combat this (and make the path more
     * interesting), we do one final pass making random flips. */

    /* Set to true for final pass */
    do_random_pass = false;

    while (true) {
        /* Remember whether a flip occurred during this pass */
        bool flipped = false;

        for (i = 0; i < num_faces; ++i) {
            int j = face_list[i];
            enum face_colour opp =
                (board[j] == FACE_WHITE) ? FACE_BLACK : FACE_WHITE;
            if (can_colour_face(g, board, j, opp)) {
                grid_face *face = g->faces +j;
                if (do_random_pass) {
                    /* final random pass */
                    if (!random_upto(rs, 10))
                        board[j] = opp;
                } else {
                    /* normal pass - flip when neighbour count is 1 */
                    if (face_num_neighbours(g, board, face, opp) == 1) {
                        board[j] = opp;
                        flipped = true;
                    }
                }
            }
        }

        if (do_random_pass) break;
        if (!flipped) do_random_pass = true;
    }

    sfree(face_list);
}
