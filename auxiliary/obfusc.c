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

int main(int argc, char **argv)
{
    enum { BINARY, DEFAULT, HEX } outputmode = DEFAULT;
    char *inhex = NULL;
    unsigned char *data;
    int datalen;
    enum { UNKNOWN, DECODE, ENCODE } mode = UNKNOWN;
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
	return 0;
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
