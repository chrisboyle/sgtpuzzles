/*
 * gtk.c: GTK front end for my puzzle collection.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "puzzles.h"

void fatal(char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "fatal error: ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    exit(1);
}
