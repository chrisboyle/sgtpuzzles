/*
 * random.c: Internal random number generator, guaranteed to work
 * the same way on all platforms. Used when generating an initial
 * game state from a random game seed; required to ensure that game
 * seeds can be exchanged between versions of a puzzle compiled for
 * different platforms.
 * 
 * The generator is based on SHA-1. This is almost certainly
 * overkill, but I had the SHA-1 code kicking around and it was
 * easier to reuse it than to do anything else!
 */

#include <assert.h>

#include "puzzles.h"

typedef unsigned long uint32;

typedef struct {
    uint32 h[5];
    unsigned char block[64];
    int blkused;
    uint32 lenhi, lenlo;
} SHA_State;

/* ----------------------------------------------------------------------
 * Core SHA algorithm: processes 16-word blocks into a message digest.
 */

#define rol(x,y) ( ((x) << (y)) | (((uint32)x) >> (32-y)) )

static void SHA_Core_Init(uint32 h[5])
{
    h[0] = 0x67452301;
    h[1] = 0xefcdab89;
    h[2] = 0x98badcfe;
    h[3] = 0x10325476;
    h[4] = 0xc3d2e1f0;
}

static void SHATransform(uint32 * digest, uint32 * block)
{
    uint32 w[80];
    uint32 a, b, c, d, e;
    int t;

    for (t = 0; t < 16; t++)
	w[t] = block[t];

    for (t = 16; t < 80; t++) {
	uint32 tmp = w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16];
	w[t] = rol(tmp, 1);
    }

    a = digest[0];
    b = digest[1];
    c = digest[2];
    d = digest[3];
    e = digest[4];

    for (t = 0; t < 20; t++) {
	uint32 tmp =
	    rol(a, 5) + ((b & c) | (d & ~b)) + e + w[t] + 0x5a827999;
	e = d;
	d = c;
	c = rol(b, 30);
	b = a;
	a = tmp;
    }
    for (t = 20; t < 40; t++) {
	uint32 tmp = rol(a, 5) + (b ^ c ^ d) + e + w[t] + 0x6ed9eba1;
	e = d;
	d = c;
	c = rol(b, 30);
	b = a;
	a = tmp;
    }
    for (t = 40; t < 60; t++) {
	uint32 tmp = rol(a,
			 5) + ((b & c) | (b & d) | (c & d)) + e + w[t] +
	    0x8f1bbcdc;
	e = d;
	d = c;
	c = rol(b, 30);
	b = a;
	a = tmp;
    }
    for (t = 60; t < 80; t++) {
	uint32 tmp = rol(a, 5) + (b ^ c ^ d) + e + w[t] + 0xca62c1d6;
	e = d;
	d = c;
	c = rol(b, 30);
	b = a;
	a = tmp;
    }

    digest[0] += a;
    digest[1] += b;
    digest[2] += c;
    digest[3] += d;
    digest[4] += e;
}

/* ----------------------------------------------------------------------
 * Outer SHA algorithm: take an arbitrary length byte string,
 * convert it into 16-word blocks with the prescribed padding at
 * the end, and pass those blocks to the core SHA algorithm.
 */

static void SHA_Init(SHA_State * s)
{
    SHA_Core_Init(s->h);
    s->blkused = 0;
    s->lenhi = s->lenlo = 0;
}

static void SHA_Bytes(SHA_State * s, void *p, int len)
{
    unsigned char *q = (unsigned char *) p;
    uint32 wordblock[16];
    uint32 lenw = len;
    int i;

    /*
     * Update the length field.
     */
    s->lenlo += lenw;
    s->lenhi += (s->lenlo < lenw);

    if (s->blkused && s->blkused + len < 64) {
	/*
	 * Trivial case: just add to the block.
	 */
	memcpy(s->block + s->blkused, q, len);
	s->blkused += len;
    } else {
	/*
	 * We must complete and process at least one block.
	 */
	while (s->blkused + len >= 64) {
	    memcpy(s->block + s->blkused, q, 64 - s->blkused);
	    q += 64 - s->blkused;
	    len -= 64 - s->blkused;
	    /* Now process the block. Gather bytes big-endian into words */
	    for (i = 0; i < 16; i++) {
		wordblock[i] =
		    (((uint32) s->block[i * 4 + 0]) << 24) |
		    (((uint32) s->block[i * 4 + 1]) << 16) |
		    (((uint32) s->block[i * 4 + 2]) << 8) |
		    (((uint32) s->block[i * 4 + 3]) << 0);
	    }
	    SHATransform(s->h, wordblock);
	    s->blkused = 0;
	}
	memcpy(s->block, q, len);
	s->blkused = len;
    }
}

static void SHA_Final(SHA_State * s, unsigned char *output)
{
    int i;
    int pad;
    unsigned char c[64];
    uint32 lenhi, lenlo;

    if (s->blkused >= 56)
	pad = 56 + 64 - s->blkused;
    else
	pad = 56 - s->blkused;

    lenhi = (s->lenhi << 3) | (s->lenlo >> (32 - 3));
    lenlo = (s->lenlo << 3);

    memset(c, 0, pad);
    c[0] = 0x80;
    SHA_Bytes(s, &c, pad);

    c[0] = (lenhi >> 24) & 0xFF;
    c[1] = (lenhi >> 16) & 0xFF;
    c[2] = (lenhi >> 8) & 0xFF;
    c[3] = (lenhi >> 0) & 0xFF;
    c[4] = (lenlo >> 24) & 0xFF;
    c[5] = (lenlo >> 16) & 0xFF;
    c[6] = (lenlo >> 8) & 0xFF;
    c[7] = (lenlo >> 0) & 0xFF;

    SHA_Bytes(s, &c, 8);

    for (i = 0; i < 5; i++) {
	output[i * 4] = (s->h[i] >> 24) & 0xFF;
	output[i * 4 + 1] = (s->h[i] >> 16) & 0xFF;
	output[i * 4 + 2] = (s->h[i] >> 8) & 0xFF;
	output[i * 4 + 3] = (s->h[i]) & 0xFF;
    }
}

static void SHA_Simple(void *p, int len, unsigned char *output)
{
    SHA_State s;

    SHA_Init(&s);
    SHA_Bytes(&s, p, len);
    SHA_Final(&s, output);
}

/* ----------------------------------------------------------------------
 * The random number generator.
 */

struct random_state {
    unsigned char seedbuf[40];
    unsigned char databuf[20];
    int pos;
};

random_state *random_init(char *seed, int len)
{
    random_state *state;

    state = snew(random_state);

    SHA_Simple(seed, len, state->seedbuf);
    SHA_Simple(state->seedbuf, 20, state->seedbuf + 20);
    SHA_Simple(state->seedbuf, 40, state->databuf);
    state->pos = 0;

    return state;
}

unsigned long random_bits(random_state *state, int bits)
{
    int ret = 0;
    int n;

    for (n = 0; n < bits; n += 8) {
	if (state->pos >= 20) {
	    int i;

	    for (i = 0; i < 20; i++) {
		if (state->seedbuf[i] != 0xFF) {
		    state->seedbuf[i]++;
		    break;
		} else
		    state->seedbuf[i] = 0;
	    }
	    SHA_Simple(state->seedbuf, 40, state->databuf);
	    state->pos = 0;
	}
	ret = (ret << 8) | state->databuf[state->pos++];
    }

    ret &= (1 << bits) - 1;
    return ret;
}

unsigned long random_upto(random_state *state, unsigned long limit)
{
    int bits = 0;
    unsigned long max, divisor, data;

    while ((limit >> bits) != 0)
	bits++;

    bits += 3;
    assert(bits < 32);

    max = 1 << bits;
    divisor = max / limit;
    max = limit * divisor;

    do {
	data = random_bits(state, bits);
    } while (data >= max);

    return data / divisor;
}

void random_free(random_state *state)
{
    sfree(state);
}
