/*
 * misc.c: Miscellaneous helpful functions.
 */

#include <assert.h>
#include <ctype.h>
#ifdef NO_TGMATH_H
#  include <math.h>
#else
#  include <tgmath.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "puzzles.h"

char MOVE_UI_UPDATE[] = "";
char MOVE_NO_EFFECT[] = "";
char MOVE_UNUSED[] = "";

void free_cfg(config_item *cfg)
{
    config_item *i;

    for (i = cfg; i->type != C_END; i++)
	if (i->type == C_STRING)
	    sfree(i->u.string.sval);
    sfree(cfg);
}

void free_keys(key_label *keys, int nkeys)
{
    int i;

    for(i = 0; i < nkeys; i++)
        sfree(keys[i].label);
    sfree(keys);
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
void obfuscate_bitmap(unsigned char *bmp, int bits, bool decode)
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

char *fgetline(FILE *fp)
{
    char *ret = snewn(512, char);
    int size = 512, len = 0;
    while (fgets(ret + len, size - len, fp)) {
	len += strlen(ret + len);
	if (ret[len-1] == '\n')
	    break;		       /* got a newline, we're done */
	size = len + 512;
	ret = sresize(ret, size, char);
    }
    if (len == 0) {		       /* first fgets returned NULL */
	sfree(ret);
	return NULL;
    }
    ret[len] = '\0';
    return ret;
}

int getenv_bool(const char *name, int dflt)
{
    char *env = getenv(name);
    if (env == NULL) return dflt;
    if (strchr("yYtT", env[0])) return true;
    return false;
}

/* Utility functions for colour manipulation. */

static float colour_distance(const float a[3], const float b[3])
{
    return (float)sqrt((a[0]-b[0]) * (a[0]-b[0]) +
                       (a[1]-b[1]) * (a[1]-b[1]) +
                       (a[2]-b[2]) * (a[2]-b[2]));
}

void colour_mix(const float src1[3], const float src2[3], float p, float dst[3])
{
    int i;
    for (i = 0; i < 3; i++)
        dst[i] = src1[i] * (1.0F - p) + src2[i] * p;
}

void game_mkhighlight_specific(frontend *fe, float *ret,
			       int background, int highlight, int lowlight)
{
    static const float black[3] = { 0.0F, 0.0F, 0.0F };
    static const float white[3] = { 1.0F, 1.0F, 1.0F };
    float db, dw;
    int i;
    /*
     * New geometric highlight-generation algorithm: Draw a line from
     * the base colour to white.  The point K distance along this line
     * from the base colour is the highlight colour.  Similarly, draw
     * a line from the base colour to black.  The point on this line
     * at a distance K from the base colour is the shadow.  If either
     * of these colours is imaginary (for reasonable K at most one
     * will be), _extrapolate_ the base colour along the same line
     * until it's a distance K from white (or black) and start again
     * with that as the base colour.
     *
     * This preserves the hue of the base colour, ensures that of the
     * three the base colour is the most saturated, and only ever
     * flattens the highlight and shadow to pure white or pure black.
     *
     * K must be at most sqrt(3)/2, or mid grey would be too close to
     * both white and black.  Here K is set to sqrt(3)/6 so that this
     * code produces the same results as the former code in the common
     * case where the background is grey and the highlight saturates
     * to white.
     */
    const float k = sqrt(3)/6.0F;
    if (lowlight >= 0) {
        db = colour_distance(&ret[background*3], black);
        if (db < k) {
            for (i = 0; i < 3; i++) ret[lowlight*3+i] = black[i];
            if (db == 0.0F)
                colour_mix(black, white, k/sqrt(3), &ret[background*3]);
            else
                colour_mix(black, &ret[background*3], k/db, &ret[background*3]);
        } else {
            colour_mix(&ret[background*3], black, k/db, &ret[lowlight*3]);
        }
    }
    if (highlight >= 0) {
        dw = colour_distance(&ret[background*3], white);
        if (dw < k) {
            for (i = 0; i < 3; i++) ret[highlight*3+i] = white[i];
            if (dw == 0.0F)
                colour_mix(white, black, k/sqrt(3), &ret[background*3]);
            else
                colour_mix(white, &ret[background*3], k/dw, &ret[background*3]);
            /* Background has changed; recalculate lowlight. */
            if (lowlight >= 0)
                colour_mix(&ret[background*3], black, k/db, &ret[lowlight*3]);
        } else {
            colour_mix(&ret[background*3], white, k/dw, &ret[highlight*3]);
        }
    }
}

void game_mkhighlight(frontend *fe, float *ret,
                      int background, int highlight, int lowlight)
{
    frontend_default_colour(fe, &ret[background * 3]);
    game_mkhighlight_specific(fe, ret, background, highlight, lowlight);
}

void swap_regions(void *av, void *bv, size_t size)
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
            swap_regions(carray + eltsize * i, carray + eltsize * j, eltsize);
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

int compare_integers(const void *av, const void *bv) {
    const int *a = (const int *)av;
    const int *b = (const int *)bv;
    if (*a < *b)
	return -1;
    else if (*a > *b)
	return +1;
    else
	return 0;
}

char *move_cursor(int button, int *x, int *y, int maxw, int maxh, bool wrap,
    bool *visible)
{
    int dx = 0, dy = 0, ox = *x, oy = *y;
    switch (button) {
    case CURSOR_UP:         dy = -1; break;
    case CURSOR_DOWN:       dy = 1; break;
    case CURSOR_RIGHT:      dx = 1; break;
    case CURSOR_LEFT:       dx = -1; break;
    default: return MOVE_UNUSED;
    }
    if (wrap) {
        *x = (*x + dx + maxw) % maxw;
        *y = (*y + dy + maxh) % maxh;
    } else {
        *x = min(max(*x+dx, 0), maxw - 1);
        *y = min(max(*y+dy, 0), maxh - 1);
    }
    if (visible != NULL && !*visible) {
        *visible = true;
        return MOVE_UI_UPDATE;
    }
    if (*x != ox || *y != oy)
        return MOVE_UI_UPDATE;
    return MOVE_NO_EFFECT;
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
                       int text_colour, int outline_colour, const char *text)
{
    if (outline_colour > -1) {
        draw_text(dr, x-1, y, fonttype, fontsize, align, outline_colour, text);
        draw_text(dr, x+1, y, fonttype, fontsize, align, outline_colour, text);
        draw_text(dr, x, y-1, fonttype, fontsize, align, outline_colour, text);
        draw_text(dr, x, y+1, fonttype, fontsize, align, outline_colour, text);
    }
    draw_text(dr, x, y, fonttype, fontsize, align, text_colour, text);

}

/* kludge for sprintf() in Rockbox not supporting "%-8.8s" */
void copy_left_justified(char *buf, size_t sz, const char *str)
{
    size_t len = strlen(str);
    assert(sz > 0);
    memset(buf, ' ', sz - 1);
    assert(len <= sz - 1);
    memcpy(buf, str, len);
    buf[sz - 1] = 0;
}

/* Returns a dynamically allocated label for a generic button.
 * Game-specific buttons should go into the `label' field of key_label
 * instead. */
char *button2label(int button)
{
    /* check if it's a keyboard button */
    if(('A' <= button && button <= 'Z') ||
       ('a' <= button && button <= 'z') ||
       ('0' <= button && button <= '9') )
    {
        char str[2];
        str[0] = button;
        str[1] = '\0';
        return dupstr(str);
    }

    switch(button)
    {
    case CURSOR_UP:
        return dupstr("Up");
    case CURSOR_DOWN:
        return dupstr("Down");
    case CURSOR_LEFT:
        return dupstr("Left");
    case CURSOR_RIGHT:
        return dupstr("Right");
    case CURSOR_SELECT:
        return dupstr("Select");
    case '\b':
        return dupstr("Clear");
    default:
        fatal("unknown generic key");
    }

    /* should never get here */
    return NULL;
}

char *make_prefs_path(const char *dir, const char *sep,
                      const game *game, const char *suffix)
{
    size_t dirlen = strlen(dir);
    size_t seplen = strlen(sep);
    size_t gamelen = strlen(game->name);
    size_t suffixlen = strlen(suffix);
    char *path, *p;
    const char *q;

    if (!dir || !sep || !game || !suffix)
        return NULL;

    path = snewn(dirlen + seplen + gamelen + suffixlen + 1, char);
    p = path;

    memcpy(p, dir, dirlen);
    p += dirlen;

    memcpy(p, sep, seplen);
    p += seplen;

    for (q = game->name; *q; q++)
        if (*q != ' ')
            *p++ = tolower((unsigned char)*q);

    memcpy(p, suffix, suffixlen);
    p += suffixlen;

    *p = '\0';
    return path;
}

/*
 * Calculate the nearest integer to n*sqrt(k), via a bitwise algorithm
 * that avoids floating point.
 *
 * (It would probably be OK in practice to use floating point, but I
 * felt like overengineering it for fun. With FP, there's at least a
 * theoretical risk of rounding the wrong way, due to the three
 * successive roundings involved - rounding sqrt(k), rounding its
 * product with n, and then rounding to the nearest integer. This
 * approach avoids that: it's exact.)
 */
int n_times_root_k(int n_signed, int k)
{
    unsigned x, r, m;
    int sign = n_signed < 0 ? -1 : +1;
    unsigned n = n_signed * sign;
    unsigned bitpos;

    /*
     * Method:
     *
     * We transform m gradually from zero into n, by multiplying it by
     * 2 in each step and optionally adding 1, so that it's always
     * floor(n/2^something).
     *
     * At the start of each step, x is the largest integer less than
     * or equal to m*sqrt(k). We transform m to 2m+bit, and therefore
     * we must transform x to 2x+something to match. The 'something'
     * we add to 2x is at most floor(sqrt(k))+2. (Worst case is if m
     * sqrt(k) was equal to x + 1-eps for some tiny eps, and then the
     * incoming bit of m is 1, so that (2m+1)sqrt(k) =
     * 2x+2+sqrt(k)-2eps.)
     *
     * To compute this, we also track the residual value r such that
     * x^2+r = km^2.
     *
     * The algorithm below is very similar to the usual approach for
     * taking the square root of an integer in binary. The wrinkle is
     * that we have an integer multiplier, i.e. we're computing
     * n*sqrt(k) rather than just sqrt(k). Of course in principle we
     * could just take sqrt(n^2k), but we'd need an integer twice the
     * width to hold n^2. Pulling out n and treating it specially
     * makes overflow less likely.
     */

    x = r = m = 0;

    for (bitpos = UINT_MAX & ~(UINT_MAX >> 1); bitpos; bitpos >>= 1) {
        unsigned a, b = (n & bitpos) ? 1 : 0;

        /*
         * Check invariants. We expect that x^2 + r = km^2 (i.e. our
         * residual term is correct), and also that r < 2x+1 (because
         * if not, then we could replace x with x+1 and still get a
         * value that made r non-negative, i.e. x would not be the
         * _largest_ integer less than m sqrt(k)).
         */
        assert(x*x + r == k*m*m);
        assert(r < 2*x+1);

        /*
         * We're going to replace m with 2m+b, and x with 2x+a for
         * some a we haven't decided on yet.
         *
         * The new value of the residual will therefore be
         *
         *   k (2m+b)^2 - (2x+a)^2
         * = (4km^2 + 4kmb + kb^2) - (4x^2 + 4xa + a^2)
         * = 4 (km^2 - x^2) + 4kmb + kb^2 - 4xa - a^2
         * = 4r + 4kmb + kb^2 - 4xa - a^2          (because r = km^2 - x^2)
         * = 4r + (4m + 1)kb - 4xa - a^2           (b is 0 or 1, so b = b^2)
         */
        for (a = 0;; a++) {
            /* If we made this routine handle square roots of numbers
             * significantly bigger than 3 or 5 then it would be
             * sensible to make this a binary search. Here, it hardly
             * seems important. */
            unsigned pos = 4*r + k*b*(4*m + 1);
            unsigned neg = 4*a*x + a*a;
            if (pos < neg)
                break;                 /* this value of a is too big */
        }

        /* The above loop will have terminated with a one too big. So
         * now decrementing a will give us the right value to add. */
        a--;

        r = 4*r + b*k*(4*m + 1) - (4*a*x + a*a);
        m = 2*m+b;
        x = 2*x+a;
    }

    /*
     * Finally, round to the nearest integer. At present, x is the
     * largest integer that is _at most_ m sqrt(k). But we want the
     * _nearest_ integer, whether that's rounded up or down. So check
     * whether (x + 1/2) is still less than m sqrt(k), i.e. whether
     * (x + 1/2)^2 < km^2; if it is, then we increment x.
     *
     * We have km^2 - (x + 1/2)^2 = km^2 - x^2 - x - 1/4
     *                            = r - x - 1/4
     *
     * and since r and x are integers, this is greater than 0 if and
     * only if r > x.
     *
     * (There's no need to worry about tie-breaking exact halfway
     * rounding cases. sqrt(k) is irrational, so none such exist.)
     */
    if (r > x)
        x++;

    /*
     * Put the sign back on, and convert back from unsigned to int.
     */
    if (sign == +1) {
        return x;
    } else {
        /* Be a little careful to avoid compilers deciding I've just
         * perpetrated signed-integer overflow. This should optimise
         * down to no actual code. */
        return INT_MIN + (int)(-x - (unsigned)INT_MIN);
    }
}

/* vim: set shiftwidth=4 tabstop=8: */
