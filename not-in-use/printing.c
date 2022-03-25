/*
 * printing.c: Cross-platform printing manager. Handles document
 * setup and layout.
 */

#include <assert.h>

#include "puzzles.h"

struct puzzle {
    const game *game;
    game_params *par;
    game_state *st;
    game_state *st2;
};

struct document {
    int pw, ph;
    int npuzzles;
    struct puzzle *puzzles;
    int puzzlesize;
    bool got_solns;
    float *colwid, *rowht;
    float userscale;
};

/*
 * Create a new print document. pw and ph are the layout
 * parameters: they state how many puzzles will be printed across
 * the page, and down the page.
 */
document *document_new(int pw, int ph, float userscale)
{
    document *doc = snew(document);

    doc->pw = pw;
    doc->ph = ph;
    doc->puzzles = NULL;
    doc->puzzlesize = doc->npuzzles = 0;
    doc->got_solns = false;

    doc->colwid = snewn(pw, float);
    doc->rowht = snewn(ph, float);

    doc->userscale = userscale;

    return doc;
}

/*
 * Free a document structure, whether it's been printed or not.
 */
void document_free(document *doc)
{
    int i;

    for (i = 0; i < doc->npuzzles; i++) {
	doc->puzzles[i].game->free_params(doc->puzzles[i].par);
	doc->puzzles[i].game->free_game(doc->puzzles[i].st);
	if (doc->puzzles[i].st2)
	    doc->puzzles[i].game->free_game(doc->puzzles[i].st2);
    }

    sfree(doc->colwid);
    sfree(doc->rowht);

    sfree(doc->puzzles);
    sfree(doc);
}

/*
 * Called from midend.c to add a puzzle to be printed. Provides a
 * game_params (for initial layout computation), a game_state, and
 * optionally a second game_state to be printed in parallel on
 * another sheet (typically the solution to the first game_state).
 */
void document_add_puzzle(document *doc, const game *game, game_params *par,
			 game_state *st, game_state *st2)
{
    if (doc->npuzzles >= doc->puzzlesize) {
	doc->puzzlesize += 32;
	doc->puzzles = sresize(doc->puzzles, doc->puzzlesize, struct puzzle);
    }
    doc->puzzles[doc->npuzzles].game = game;
    doc->puzzles[doc->npuzzles].par = par;
    doc->puzzles[doc->npuzzles].st = st;
    doc->puzzles[doc->npuzzles].st2 = st2;
    doc->npuzzles++;
    if (st2)
	doc->got_solns = true;
}

static void get_puzzle_size(const document *doc, struct puzzle *pz,
			    float *w, float *h, float *scale)
{
    float ww, hh, ourscale;

    /* Get the preferred size of the game, in mm. */
    pz->game->print_size(pz->par, &ww, &hh);

    /* Adjust for user-supplied scale factor. */
    ourscale = doc->userscale;

    /*
     * FIXME: scale it down here if it's too big for the page size.
     * Rather than do complicated things involving scaling all
     * columns down in proportion, the simplest approach seems to
     * me to be to scale down until the game fits within one evenly
     * divided cell of the page (i.e. width/pw by height/ph).
     * 
     * In order to do this step we need the page size available.
     */

    *scale = ourscale;
    *w = ww * ourscale;
    *h = hh * ourscale;
}

/*
 * Calculate the the number of pages for a document.
 */
int document_npages(const document *doc)
{
    int ppp;			       /* puzzles per page */
    int pages, passes;

    ppp = doc->pw * doc->ph;
    pages = (doc->npuzzles + ppp - 1) / ppp;
    passes = (doc->got_solns ? 2 : 1);

    return pages * passes;
}

/*
 * Begin a document.
 */
void document_begin(const document *doc, drawing *dr)
{
    print_begin_doc(dr, document_npages(doc));
}

/*
 * End a document.
 */
void document_end(const document *doc, drawing *dr)
{
    print_end_doc(dr);
}

/*
 * Print a single page of a document.
 */
void document_print_page(const document *doc, drawing *dr, int page_nr)
{
    int ppp;			       /* puzzles per page */
    int pages;
    int page, pass;
    int pageno;
    int i, n, offset;
    float colsum, rowsum;

    ppp = doc->pw * doc->ph;
    pages = (doc->npuzzles + ppp - 1) / ppp;

    /* Get the current page, pass, and pageno based on page_nr. */
    if (page_nr < pages) {
        page = page_nr;
        pass = 0;
    }
    else {
        assert(doc->got_solns);
        page = page_nr - pages;
        pass = 1;
    }
    pageno = page_nr + 1;

    offset = page * ppp;
    n = min(ppp, doc->npuzzles - offset);

    print_begin_page(dr, pageno);

    for (i = 0; i < doc->pw; i++)
        doc->colwid[i] = 0;
    for (i = 0; i < doc->ph; i++)
        doc->rowht[i] = 0;

    /*
     * Lay the page out by computing all the puzzle sizes.
     */
    for (i = 0; i < n; i++) {
        struct puzzle *pz = doc->puzzles + offset + i;
        int x = i % doc->pw, y = i / doc->pw;
        float w, h, scale;

        get_puzzle_size(doc, pz, &w, &h, &scale);

        /* Update the maximum width/height of this column. */
        doc->colwid[x] = max(doc->colwid[x], w);
        doc->rowht[y] = max(doc->rowht[y], h);
    }

    /*
     * Add up the maximum column/row widths to get the
     * total amount of space used up by puzzles on the
     * page. We will use this to compute gutter widths.
     */
    colsum = 0.0;
    for (i = 0; i < doc->pw; i++)
        colsum += doc->colwid[i];
    rowsum = 0.0;
    for (i = 0; i < doc->ph; i++)
        rowsum += doc->rowht[i];

    /*
     * Now do the printing.
     */
    for (i = 0; i < n; i++) {
        struct puzzle *pz = doc->puzzles + offset + i;
        int x = i % doc->pw, y = i / doc->pw, j;
        float w, h, scale, xm, xc, ym, yc;
        int pixw, pixh, tilesize;

        if (pass == 1 && !pz->st2)
            continue;	       /* nothing to do */

        /*
         * The total amount of gutter space is the page
         * width minus colsum. This is divided into pw+1
         * gutters, so the amount of horizontal gutter
         * space appearing to the left of this puzzle
         * column is
         *
         *   (width-colsum) * (x+1)/(pw+1)
         * = width * (x+1)/(pw+1) - (colsum * (x+1)/(pw+1))
         */
        xm = (float)(x+1) / (doc->pw + 1);
        xc = -xm * colsum;
        /* And similarly for y. */
        ym = (float)(y+1) / (doc->ph + 1);
        yc = -ym * rowsum;

        /*
         * However, the amount of space to the left of this
         * puzzle isn't just gutter space: we must also
         * count the widths of all the previous columns.
         */
        for (j = 0; j < x; j++)
            xc += doc->colwid[j];
        /* And similarly for rows. */
        for (j = 0; j < y; j++)
            yc += doc->rowht[j];

        /*
         * Now we adjust for this _specific_ puzzle, which
         * means centring it within the cell we've just
         * computed.
         */
        get_puzzle_size(doc, pz, &w, &h, &scale);
        xc += (doc->colwid[x] - w) / 2;
        yc += (doc->rowht[y] - h) / 2;

        /*
         * And now we know where and how big we want to
         * print the puzzle, just go ahead and do so. For
         * the moment I'll pick a standard pixel tile size
         * of 512.
         *
         * (FIXME: would it be better to pick this value
         * with reference to the printer resolution? Or
         * permit each game to choose its own?)
         */
        tilesize = 512;
        pz->game->compute_size(pz->par, tilesize, &pixw, &pixh);
        print_begin_puzzle(dr, xm, xc, ym, yc, pixw, pixh, w, scale);
        pz->game->print(dr, pass == 0 ? pz->st : pz->st2, tilesize);
        print_end_puzzle(dr);
    }

    print_end_page(dr, pageno);
}

/*
 * Having accumulated a load of puzzles, actually do the printing.
 */
void document_print(const document *doc, drawing *dr)
{
    int page, pages;
    pages = document_npages(doc);
    print_begin_doc(dr, pages);
    for (page = 0; page < pages; page++)
        document_print_page(doc, dr, page);
    print_end_doc(dr);
}
