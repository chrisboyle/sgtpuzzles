/*
 * fuzzpuzz.c: Fuzzing frontend to all puzzles.
 */

/*
 * The idea here is that this front-end supports all back-ends and can
 * feed them save files.  This tests the deserialiser, the code for
 * loading game descriptions, and the processing of move strings,
 * without all the tedium of actually rendering anything.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "puzzles.h"

#ifdef __AFL_FUZZ_TESTCASE_LEN
/*
 * AFL persistent mode, where we fuzz from a RAM buffer provided by
 * AFL in a loop.  This version can still be run standalone if
 * necessary, for instance to diagnose a crash.
 */
#include <unistd.h>

__AFL_FUZZ_INIT();

struct memfile {
    unsigned char *buf;
    int off;
    int len;
};

static bool memfile_read(void *wctx, void *buf, int len)
{
    struct memfile *mem = (struct memfile *)wctx;

    if (mem->len - mem->off < len) return false;
    memcpy(buf, mem->buf + mem->off, min(len, mem->len - mem->off));
    mem->off += len;
    return true;
}

int main(int argc, char **argv)
{
    const char *err;
    char *gamename;
    int i;
    const game * ourgame = NULL;
    midend *me;
    struct memfile mem;

#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    mem.buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(10000)) {

        mem.off = 0;
        mem.len = __AFL_FUZZ_TESTCASE_LEN;

        err = identify_game(&gamename, memfile_read, &mem);
        if (err != NULL) continue;

        for (i = 0; i < gamecount; i++)
            if (strcmp(gamename, gamelist[i]->name) == 0)
                ourgame = gamelist[i];
        if (ourgame == NULL) continue;

        me = midend_new(NULL, ourgame, NULL, NULL);

        mem.off = 0;

        err = midend_deserialise(me, memfile_read, &mem);
        midend_free(me);
    }
    return 0;
}

#else

/*
 * Standard mode, where we process a single save file from stdin.
 */

static bool savefile_read(void *wctx, void *buf, int len)
{
    FILE *fp = (FILE *)wctx;
    int ret;

    ret = fread(buf, 1, len, fp);
    return (ret == len);
}

int main(int argc, char **argv)
{
    const char *err;
    char *gamename;
    int i;
    const game * ourgame = NULL;
    midend *me;

    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        exit(1);
    }

    err = identify_game(&gamename, savefile_read, stdin);
    if (err != NULL) {
        fprintf(stderr, "%s\n", err);
        exit(1);
    }

    for (i = 0; i < gamecount; i++)
        if (strcmp(gamename, gamelist[i]->name) == 0)
            ourgame = gamelist[i];
    if (ourgame == NULL) {
        fprintf(stderr, "Game '%s' not recognised\n", gamename);
        exit(1);
    }

    me = midend_new(NULL, ourgame, NULL, NULL);

    rewind(stdin);
    err = midend_deserialise(me, savefile_read, stdin);
    if (err != NULL) {
        fprintf(stderr, "%s\n", err);
        exit(1);
    }
    midend_free(me);
    return 0;
}

#endif
