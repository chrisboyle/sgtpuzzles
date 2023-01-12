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
#ifdef __AFL_FUZZ_TESTCASE_LEN
# include <unistd.h> /* read() is used by __AFL_FUZZ_TESTCASE_LEN. */
#endif

#include "puzzles.h"

#ifdef __AFL_FUZZ_INIT
__AFL_FUZZ_INIT();
#endif

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
    int i, ret = -1;
    const game *ourgame = NULL;
    midend *me;
    FILE *in = NULL;

    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        exit(1);
    }

#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

#ifdef __AFL_FUZZ_TESTCASE_LEN
    /*
     * AFL persistent mode, where we fuzz from a RAM buffer provided
     * by AFL in a loop.  This version can still be run standalone if
     * necessary, for instance to diagnose a crash.
     */

    while (__AFL_LOOP(10000)) {
        if (in != NULL) fclose(in);
        in = fmemopen(__AFL_FUZZ_TESTCASE_BUF, __AFL_FUZZ_TESTCASE_LEN, "r");
        if (in == NULL) {
            fprintf(stderr, "fmemopen failed");
            ret = 1;
            continue;
        }
#else
    in = stdin;
    while (ret == -1) {
#endif
        err = identify_game(&gamename, savefile_read, in);
        if (err != NULL) {
            fprintf(stderr, "%s\n", err);
            ret = 1;
            continue;
        }

        for (i = 0; i < gamecount; i++)
            if (strcmp(gamename, gamelist[i]->name) == 0)
                ourgame = gamelist[i];
        sfree(gamename);
        if (ourgame == NULL) {
            fprintf(stderr, "Game '%s' not recognised\n", gamename);
            ret = 1;
            continue;
        }

        me = midend_new(NULL, ourgame, NULL, NULL);

        rewind(in);
        err = midend_deserialise(me, savefile_read, in);
        if (err != NULL) {
            fprintf(stderr, "%s\n", err);
            ret = 1;
            midend_free(me);
            continue;
        }
        midend_free(me);
        ret = 0;
    }
    return ret;
}
