/*
 * misc.c: Miscellaneous helpful functions.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "puzzles.h"

void free_cfg(config_item *cfg)
{
    config_item *i;

    if (! cfg) return;
    for (i = cfg; i->type != C_END; i++)
	if (i->type == C_STRING)
	    sfree(i->sval);
    sfree(cfg);
}

/*
 * The Mines (among others) game descriptions contain the location of every
 * mine, and can therefore be used to cheat.
 *
 * It would be pointless to attempt to _prevent_ this form of
 * cheating by encrypting the description, since Mines is
 * open-source so anyone can find out the encryption key. However,
 * I think it is worth doing a bit of gentle obfuscation to prevent
 * _accidental_ spoilers: if you happened to note that the game ID
 * starts with an F, for example, you might be unable to put the
 * knowledge of those mines out of your mind while playing. So,
 * just as discussions of film endings are rot13ed to avoid
 * spoiling it for people who don't want to be told, we apply a
 * keyless, reversible, but visually completely obfuscatory masking
 * function to the mine bitmap.
 */
void obfuscate_bitmap(unsigned char *bmp, int bits, int decode)
{
    int bytes, firsthalf, secondhalf;
    struct step {
	unsigned char *seedstart;
	int seedlen;
	unsigned char *targetstart;
	int targetlen;
    } steps[2];
    int i, j;

    /*
     * My obfuscation algorithm is similar in concept to the OAEP
     * encoding used in some forms of RSA. Here's a specification
     * of it:
     * 
     * 	+ We have a `masking function' which constructs a stream of
     * 	  pseudorandom bytes from a seed of some number of input
     * 	  bytes.
     * 
     * 	+ We pad out our input bit stream to a whole number of
     * 	  bytes by adding up to 7 zero bits on the end. (In fact
     * 	  the bitmap passed as input to this function will already
     * 	  have had this done in practice.)
     * 
     * 	+ We divide the _byte_ stream exactly in half, rounding the
     * 	  half-way position _down_. So an 81-bit input string, for
     * 	  example, rounds up to 88 bits or 11 bytes, and then
     * 	  dividing by two gives 5 bytes in the first half and 6 in
     * 	  the second half.
     * 
     * 	+ We generate a mask from the second half of the bytes, and
     * 	  XOR it over the first half.
     * 
     * 	+ We generate a mask from the (encoded) first half of the
     * 	  bytes, and XOR it over the second half. Any null bits at
     * 	  the end which were added as padding are cleared back to
     * 	  zero even if this operation would have made them nonzero.
     * 
     * To de-obfuscate, the steps are precisely the same except
     * that the final two are reversed.
     * 
     * Finally, our masking function. Given an input seed string of
     * bytes, the output mask consists of concatenating the SHA-1
     * hashes of the seed string and successive decimal integers,
     * starting from 0.
     */

    bytes = (bits + 7) / 8;
    firsthalf = bytes / 2;
    secondhalf = bytes - firsthalf;

    steps[decode ? 1 : 0].seedstart = bmp + firsthalf;
    steps[decode ? 1 : 0].seedlen = secondhalf;
    steps[decode ? 1 : 0].targetstart = bmp;
    steps[decode ? 1 : 0].targetlen = firsthalf;

    steps[decode ? 0 : 1].seedstart = bmp;
    steps[decode ? 0 : 1].seedlen = firsthalf;
    steps[decode ? 0 : 1].targetstart = bmp + firsthalf;
    steps[decode ? 0 : 1].targetlen = secondhalf;

    for (i = 0; i < 2; i++) {
	SHA_State base, final;
	unsigned char digest[20];
	char numberbuf[80];
	int digestpos = 20, counter = 0;

	SHA_Init(&base);
	SHA_Bytes(&base, steps[i].seedstart, steps[i].seedlen);

	for (j = 0; j < steps[i].targetlen; j++) {
	    if (digestpos >= 20) {
		sprintf(numberbuf, "%d", counter++);
		final = base;
		SHA_Bytes(&final, numberbuf, strlen(numberbuf));
		SHA_Final(&final, digest);
		digestpos = 0;
	    }
	    steps[i].targetstart[j] ^= digest[digestpos++];
	}

	/*
	 * Mask off the pad bits in the final byte after both steps.
	 */
	if (bits % 8)
	    bmp[bits / 8] &= 0xFF & (0xFF00 >> (bits % 8));
    }
}

/* err, yeah, these two pretty much rely on unsigned char being 8 bits.
 * Platforms where this is not the case probably have bigger problems
 * than just making these two work, though... */
char *bin2hex(const unsigned char *in, int inlen)
{
    char *ret = snewn(inlen*2 + 1, char), *p = ret;
    int i;

    for (i = 0; i < inlen*2; i++) {
        int v = in[i/2];
        if (i % 2 == 0) v >>= 4;
        *p++ = "0123456789abcdef"[v & 0xF];
    }
    *p = '\0';
    return ret;
}

unsigned char *hex2bin(const char *in, int outlen)
{
    unsigned char *ret = snewn(outlen, unsigned char);
    int i;

    memset(ret, 0, outlen*sizeof(unsigned char));
    for (i = 0; i < outlen*2; i++) {
        int c = in[i];
        int v;

        assert(c != 0);
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        else
            v = 0;

        ret[i / 2] |= v << (4 * (1 - (i % 2)));
    }
    return ret;
}

void game_mkhighlight_specific(frontend *fe, float *ret,
			       int background, int highlight, int lowlight)
{
    float max;
    int i;

    /*
     * Drop the background colour so that the highlight is
     * noticeably brighter than it while still being under 1.
     */
    max = ret[background*3];
    for (i = 1; i < 3; i++)
        if (ret[background*3+i] > max)
            max = ret[background*3+i];
    if (max * 1.2F > 1.0F) {
        for (i = 0; i < 3; i++)
            ret[background*3+i] /= (max * 1.2F);
    }

    for (i = 0; i < 3; i++) {
	if (highlight >= 0)
	    ret[highlight * 3 + i] = ret[background * 3 + i] * 1.2F;
	if (lowlight >= 0)
	    ret[lowlight * 3 + i] = ret[background * 3 + i] * 0.8F;
    }
}

void game_mkhighlight(frontend *fe, float *ret,
                      int background, int highlight, int lowlight)
{
    frontend_default_colour(fe, &ret[background * 3]);
    game_mkhighlight_specific(fe, ret, background, highlight, lowlight);
}

static void memswap_(void *av, void *bv, int size)
{
    char tmpbuf[512];
    char *a = av, *b = bv;

    while (size > 0) {
	int thislen = min(size, sizeof(tmpbuf));
	memcpy(tmpbuf, a, thislen);
	memcpy(a, b, thislen);
	memcpy(b, tmpbuf, thislen);
	a += thislen;
	b += thislen;
	size -= thislen;
    }
}

void shuffle(void *array, int nelts, int eltsize, random_state *rs)
{
    char *carray = (char *)array;
    int i;

    for (i = nelts; i-- > 1 ;) {
        int j = random_upto(rs, i+1);
        if (j != i)
            memswap_(carray + eltsize * i, carray + eltsize * j, eltsize);
    }
}

void draw_rect_outline(drawing *dr, int x, int y, int w, int h, int colour)
{
    int x0 = x, x1 = x+w-1, y0 = y, y1 = y+h-1;
    int coords[8];

    coords[0] = x0;
    coords[1] = y0;
    coords[2] = x0;
    coords[3] = y1;
    coords[4] = x1;
    coords[5] = y1;
    coords[6] = x1;
    coords[7] = y0;

    draw_polygon(dr, coords, 4, -1, colour);
}

void draw_rect_corners(drawing *dr, int cx, int cy, int r, int col)
{
    draw_line(dr, cx - r, cy - r, cx - r, cy - r/2, col);
    draw_line(dr, cx - r, cy - r, cx - r/2, cy - r, col);
    draw_line(dr, cx - r, cy + r, cx - r, cy + r/2, col);
    draw_line(dr, cx - r, cy + r, cx - r/2, cy + r, col);
    draw_line(dr, cx + r, cy - r, cx + r, cy - r/2, col);
    draw_line(dr, cx + r, cy - r, cx + r/2, cy - r, col);
    draw_line(dr, cx + r, cy + r, cx + r, cy + r/2, col);
    draw_line(dr, cx + r, cy + r, cx + r/2, cy + r, col);
}

void move_cursor(int button, int *x, int *y, int maxw, int maxh, int wrap)
{
    int dx = 0, dy = 0;
    switch (button) {
    case CURSOR_UP:         dy = -1; break;
    case CURSOR_DOWN:       dy = 1; break;
    case CURSOR_RIGHT:      dx = 1; break;
    case CURSOR_LEFT:       dx = -1; break;
    default: return;
    }
    if (wrap) {
        *x = (*x + dx + maxw) % maxw;
        *y = (*y + dy + maxh) % maxh;
    } else {
        *x = min(max(*x+dx, 0), maxw - 1);
        *y = min(max(*y+dy, 0), maxh - 1);
    }
}

/* Used in netslide.c and sixteen.c for cursor movement around edge. */

int c2pos(int w, int h, int cx, int cy)
{
    if (cy == -1)
        return cx;                      /* top row, 0 .. w-1 (->) */
    else if (cx == w)
        return w + cy;                  /* R col, w .. w+h -1 (v) */
    else if (cy == h)
        return w + h + (w-cx-1);        /* bottom row, w+h .. w+h+w-1 (<-) */
    else if (cx == -1)
        return w + h + w + (h-cy-1);    /* L col, w+h+w .. w+h+w+h-1 (^) */

    assert(!"invalid cursor pos!");
    return -1; /* not reached */
}

int c2diff(int w, int h, int cx, int cy, int button)
{
    int diff = 0;

    assert(IS_CURSOR_MOVE(button));

    /* Obvious moves around edge. */
    if (cy == -1)
        diff = (button == CURSOR_RIGHT) ? +1 : (button == CURSOR_LEFT) ? -1 : diff;
    if (cy == h)
        diff = (button == CURSOR_RIGHT) ? -1 : (button == CURSOR_LEFT) ? +1 : diff;
    if (cx == -1)
        diff = (button == CURSOR_UP) ? +1 : (button == CURSOR_DOWN) ? -1 : diff;
    if (cx == w)
        diff = (button == CURSOR_UP) ? -1 : (button == CURSOR_DOWN) ? +1 : diff;

    if (button == CURSOR_LEFT && cx == w && (cy == 0 || cy == h-1))
        diff = (cy == 0) ? -1 : +1;
    if (button == CURSOR_RIGHT && cx == -1 && (cy == 0 || cy == h-1))
        diff = (cy == 0) ? +1 : -1;
    if (button == CURSOR_DOWN && cy == -1 && (cx == 0 || cx == w-1))
        diff = (cx == 0) ? -1 : +1;
    if (button == CURSOR_UP && cy == h && (cx == 0 || cx == w-1))
        diff = (cx == 0) ? +1 : -1;

    debug(("cx,cy = %d,%d; w%d h%d, diff = %d", cx, cy, w, h, diff));
    return diff;
}

void pos2c(int w, int h, int pos, int *cx, int *cy)
{
    int max = w+h+w+h;

    pos = (pos + max) % max;

    if (pos < w) {
        *cx = pos; *cy = -1; return;
    }
    pos -= w;
    if (pos < h) {
        *cx = w; *cy = pos; return;
    }
    pos -= h;
    if (pos < w) {
        *cx = w-pos-1; *cy = h; return;
    }
    pos -= w;
    if (pos < h) {
      *cx = -1; *cy = h-pos-1; return;
    }
    assert(!"invalid pos, huh?"); /* limited by % above! */
}

void draw_text_outline(drawing *dr, int x, int y, int fonttype,
                       int fontsize, int align,
                       int text_colour, int outline_colour, char *text)
{
    if (outline_colour > -1) {
        draw_text(dr, x-1, y, fonttype, fontsize, align, outline_colour, text);
        draw_text(dr, x+1, y, fonttype, fontsize, align, outline_colour, text);
        draw_text(dr, x, y-1, fonttype, fontsize, align, outline_colour, text);
        draw_text(dr, x, y+1, fonttype, fontsize, align, outline_colour, text);
    }
    draw_text(dr, x, y, fonttype, fontsize, align, text_colour, text);
}

/* vim: set shiftwidth=4 tabstop=8: */
