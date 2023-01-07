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
