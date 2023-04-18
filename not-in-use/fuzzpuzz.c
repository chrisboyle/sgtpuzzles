/*
 * fuzzpuzz.c: Fuzzing frontend to all puzzles.
 */

/*
 * The idea here is that this front-end supports all back-ends and can
 * feed them save files.  It then asks the back-end to draw the puzzle
 * (through a null drawing API) and reserialises the state.  This
 * tests the deserialiser, the code for loading game descriptions, the
 * processing of move strings, the redraw code, and the serialisation
 * routines, but is still pretty quick.
 *
 * To use AFL++ to drive fuzzpuzz, you can do something like:
 *
 * CC=afl-cc cmake -B build-afl
 * cmake --build build-afl --target fuzzpuzz
 * mkdir fuzz-in && ln icons/''*.sav fuzz-in
 * afl-fuzz -i fuzz-in -o fuzz-out -x fuzzpuzz.dict -- build-afl/fuzzpuzz
 *
 * Similarly with Honggfuzz:
 *
 * CC=hfuzz-cc cmake -B build-honggfuzz
 * cmake --build build-honggfuzz --target fuzzpuzz
 * mkdir fuzz-corpus && ln icons/''*.sav fuzz-corpus
 * honggfuzz -s -i fuzz-corpus -w fuzzpuzz.dict -- build-honggfuzz/fuzzpuzz
 *
 * You can also use libFuzzer, though it's not really a good fit for
 * Puzzles.  The experimental forking mode seems to work OK:
 *
 * CC=clang cmake -B build-clang -DWITH_LIBFUZZER=Y
 * cmake --build build-clang --target fuzzpuzz
 * mkdir fuzz-corpus && ln icons/''*.sav fuzz-corpus
 * build-clang/fuzzpuzz -fork=1 -ignore_crashes=1 -dict=fuzzpuzz.dict \
 *   fuzz-corpus
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

#ifdef HAVE_HF_ITER
extern int HF_ITER(unsigned char **, size_t *);
#endif

/* This function is expected by libFuzzer. */

int LLVMFuzzerTestOneInput(unsigned char *data, size_t size);

static const char *fuzz_one(bool (*readfn)(void *, void *, int), void *rctx,
                            void (*rewindfn)(void *),
                            void (*writefn)(void *, const void *, int),
                            void *wctx)
{
    const char *err;
    char *gamename;
    int i, w, h;
    const game *ourgame = NULL;
    static const drawing_api drapi = { NULL };
    midend *me;

    err = identify_game(&gamename, readfn, rctx);
    if (err != NULL) return err;

    for (i = 0; i < gamecount; i++)
        if (strcmp(gamename, gamelist[i]->name) == 0)
            ourgame = gamelist[i];
    sfree(gamename);
    if (ourgame == NULL)
        return "Game not recognised";

    me = midend_new(NULL, ourgame, &drapi, NULL);

    rewindfn(rctx);
    err = midend_deserialise(me, readfn, rctx);
    if (err != NULL) {
        midend_free(me);
        return err;
    }
    w = h = INT_MAX;
    midend_size(me, &w, &h, false, 1);
    midend_redraw(me);
    midend_serialise(me, writefn, wctx);
    midend_free(me);
    return NULL;
}

#if defined(__AFL_FUZZ_TESTCASE_LEN) || defined(HAVE_HF_ITER) || \
    !defined(OMIT_MAIN)
static void savefile_write(void *wctx, const void *buf, int len)
{
    FILE *fp = (FILE *)wctx;

    fwrite(buf, 1, len, fp);
}
#endif

struct memread {
    const unsigned char *buf;
    size_t pos;
    size_t len;
};

static bool mem_read(void *wctx, void *buf, int len)
{
    struct memread *ctx = wctx;

    if (ctx->pos + len > ctx->len) return false;
    memcpy(buf, ctx->buf + ctx->pos, len);
    ctx->pos += len;
    return true;
}

static void mem_rewind(void *wctx)
{
    struct memread *ctx = wctx;

    ctx->pos = 0;
}

static void null_write(void *wctx, const void *buf, int len)
{
}

int LLVMFuzzerTestOneInput(unsigned char *data, size_t size) {
    struct memread ctx;

    ctx.buf = data;
    ctx.len = size;
    ctx.pos = 0;
    fuzz_one(mem_read, &ctx, mem_rewind, null_write, NULL);
    return 0;
}

#if defined(__AFL_FUZZ_TESTCASE_LEN) || defined(HAVE_HF_ITER)
static const char *fuzz_one_mem(unsigned char *data, size_t size) {
    struct memread ctx;

    ctx.buf = data;
    ctx.len = size;
    ctx.pos = 0;
    return fuzz_one(mem_read, &ctx, mem_rewind, savefile_write, stdout);
}
#endif

/* 
 * Three different versions of main(), for standalone, AFL, and
 * Honggfuzz modes.  LibFuzzer brings its own main().
 */

#ifdef OMIT_MAIN
/* Nothing. */
#elif defined(__AFL_FUZZ_TESTCASE_LEN)
/*
 * AFL persistent mode, where we fuzz from a RAM buffer provided
 * by AFL in a loop.  This version can still be run standalone if
 * necessary, for instance to diagnose a crash.
 */
int main(int argc, char **argv)
{
    const char *err;
    int ret;

    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 1;
    }
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif
    while (__AFL_LOOP(10000)) {
        err = fuzz_one_mem(__AFL_FUZZ_TESTCASE_BUF, __AFL_FUZZ_TESTCASE_LEN);
        if (err != NULL) {
            fprintf(stderr, "%s\n", err);
            ret = 1;
        } else
            ret = 0;
    }
    return ret;
}
#elif defined(HAVE_HF_ITER)
/*
 * Honggfuzz persistent mode.  Unlike AFL persistent mode, the
 * resulting executable cannot be run outside of Honggfuzz.
 */
int main(int argc, char **argv)
{
    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 1;
    }
    while (true) {
        unsigned char *testcase_buf;
        size_t testcase_len;
        HF_ITER(&testcase_buf, &testcase_len);
        fuzz_one_mem(testcase_buf, testcase_len);
    }
}
#else
/*
 * Stand-alone mode: just handle a single test case on stdin.
 */
static bool savefile_read(void *wctx, void *buf, int len)
{
    FILE *fp = (FILE *)wctx;
    int ret;

    ret = fread(buf, 1, len, fp);
    return (ret == len);
}

static void savefile_rewind(void *wctx)
{
    FILE *fp = (FILE *)wctx;

    rewind(fp);
}

int main(int argc, char **argv)
{
    const char *err;

    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 1;
    }

    /* Might in theory use this mode under AFL. */
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    err = fuzz_one(savefile_read, stdin, savefile_rewind,
                   savefile_write, stdout);
    if (err != NULL) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    return 0;
}
#endif
