/*
 * Stand-alone tool to access the Puzzles obfuscation algorithm.
 * 
 * To deobfuscate, use "obfusc -d":
 * 
 *   obfusc -d                 reads binary data from stdin, writes to stdout
 *   obfusc -d <hex string>    works on the given hex string instead of stdin
 *   obfusc -d -h              writes a hex string instead of binary to stdout
 *
 * To obfuscate, "obfusc -e":
 * 
 *   obfusc -e                 reads binary from stdin, writes hex to stdout
 *   obfusc -e <hex string>    works on the given hex string instead of stdin
 *   obfusc -e -b              writes binary instead of text to stdout
 *
 * The default output format is hex for -e and binary for -d
 * because that's the way obfuscation is generally used in
 * Puzzles. Either of -b and -h can always be specified to set it
 * explicitly.
 *
 * Data read from standard input is assumed always to be binary;
 * data provided on the command line is taken to be hex.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "puzzles.h"

static bool self_tests(void)
{
    bool ok = true;

    #define FAILED (ok = false, "failed")

    /*
     * A few simple test vectors for the obfuscator.
     *
     * First test: the 28-bit stream 1234567. This divides up
     * into 1234 and 567[0]. The SHA of 56 70 30 (appending
     * "0") is 15ce8ab946640340bbb99f3f48fd2c45d1a31d30. Thus,
     * we XOR the 16-bit string 15CE into the input 1234 to get
     * 07FA. Next, we SHA that with "0": the SHA of 07 FA 30 is
     * 3370135c5e3da4fed937adc004a79533962b6391. So we XOR the
     * 12-bit string 337 into the input 567 to get 650. Thus
     * our output is 07FA650.
     */
    {
        unsigned char bmp1[] = "\x12\x34\x56\x70";
        obfuscate_bitmap(bmp1, 28, false);
        printf("test 1 encode: %s\n",
               memcmp(bmp1, "\x07\xfa\x65\x00", 4) ? FAILED : "passed");
        obfuscate_bitmap(bmp1, 28, true);
        printf("test 1 decode: %s\n",
               memcmp(bmp1, "\x12\x34\x56\x70", 4) ? FAILED : "passed");
    }
    /*
     * Second test: a long string to make sure we switch from
     * one SHA to the next correctly. My input string this time
     * is simply fifty bytes of zeroes.
     */
    {
        unsigned char bmp2[50];
        unsigned char bmp2a[50];
        memset(bmp2, 0, 50);
        memset(bmp2a, 0, 50);
        obfuscate_bitmap(bmp2, 50 * 8, false);
        /*
         * SHA of twenty-five zero bytes plus "0" is
         * b202c07b990c01f6ff2d544707f60e506019b671. SHA of
         * twenty-five zero bytes plus "1" is
         * fcb1d8b5a2f6b592fe6780b36aa9d65dd7aa6db9. Thus our
         * first half becomes
         * b202c07b990c01f6ff2d544707f60e506019b671fcb1d8b5a2.
         *
         * SHA of that lot plus "0" is
         * 10b0af913db85d37ca27f52a9f78bba3a80030db. SHA of the
         * same string plus "1" is
         * 3d01d8df78e76d382b8106f480135a1bc751d725. So the
         * second half becomes
         * 10b0af913db85d37ca27f52a9f78bba3a80030db3d01d8df78.
         */
        printf("test 2 encode: %s\n",
               memcmp(bmp2, "\xb2\x02\xc0\x7b\x99\x0c\x01\xf6\xff\x2d\x54"
                      "\x47\x07\xf6\x0e\x50\x60\x19\xb6\x71\xfc\xb1\xd8"
                      "\xb5\xa2\x10\xb0\xaf\x91\x3d\xb8\x5d\x37\xca\x27"
                      "\xf5\x2a\x9f\x78\xbb\xa3\xa8\x00\x30\xdb\x3d\x01"
                      "\xd8\xdf\x78", 50) ? FAILED : "passed");
        obfuscate_bitmap(bmp2, 50 * 8, true);
        printf("test 2 decode: %s\n",
               memcmp(bmp2, bmp2a, 50) ? FAILED : "passed");
    }

    #undef FAILED

    return ok;
}

int main(int argc, char **argv)
{
    enum { BINARY, DEFAULT, HEX } outputmode = DEFAULT;
    char *inhex = NULL;
    unsigned char *data;
    int datalen;
    enum { UNKNOWN, DECODE, ENCODE, SELFTEST } mode = UNKNOWN;
    bool doing_opts = true;

    while (--argc > 0) {
	char *p = *++argv;

	if (doing_opts && *p == '-') {
	    if (!strcmp(p, "--")) {
		doing_opts = 0;
		continue;
	    }
	    p++;
	    while (*p) {
		switch (*p) {
		  case 'e':
		    mode = ENCODE;
		    break;
		  case 'd':
		    mode = DECODE;
		    break;
		  case 't':
		    mode = SELFTEST;
		    break;
		  case 'b':
		    outputmode = BINARY;
		    break;
		  case 'h':
		    outputmode = HEX;
		    break;
		  default:
		    fprintf(stderr, "obfusc: unrecognised option '-%c'\n",
			    *p);
		    return 1;
		}
		p++;
	    }
	} else {
	    if (!inhex) {
		inhex = p;
	    } else {
		fprintf(stderr, "obfusc: expected at most one argument\n");
		return 1;
	    }
	}
    }

    if (mode == UNKNOWN) {
	fprintf(stderr, "usage: obfusc < -e | -d > [ -b | -h ] [hex data]\n");
	fprintf(stderr, "   or: obfusc -t    to run self-tests\n");
	return 0;
    }

    if (mode == SELFTEST) {
        return self_tests() ? 0 : 1;
    }

    if (outputmode == DEFAULT)
	outputmode = (mode == DECODE ? BINARY : HEX);

    if (inhex) {
	datalen = strlen(inhex) / 2;
	data = hex2bin(inhex, datalen);
    } else {
	int datasize = 4096;
	datalen = 0;
	data = snewn(datasize, unsigned char);
	while (1) {
	    int ret = fread(data + datalen, 1, datasize - datalen, stdin);
	    if (ret < 0) {
		fprintf(stderr, "obfusc: read: %s\n", strerror(errno));
		return 1;
	    } else if (ret == 0) {
		break;
	    } else {
		datalen += ret;
		if (datasize - datalen < 4096) {
		    datasize = datalen * 5 / 4 + 4096;
		    data = sresize(data, datasize, unsigned char);
		}
	    }
	}
    }

    obfuscate_bitmap(data, datalen * 8, mode == DECODE);

    if (outputmode == BINARY) {
	int ret = fwrite(data, 1, datalen, stdout);
        if (ret < 0) {
            fprintf(stderr, "obfusc: write: %s\n", strerror(errno));
            return 1;
        }
    } else {
	int i;
	for (i = 0; i < datalen; i++)
	    printf("%02x", data[i]);
	printf("\n");
    }

    return 0;
}
