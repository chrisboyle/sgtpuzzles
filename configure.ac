dnl Configure script for the Unix GTK build of puzzles.

AC_INIT([puzzles], rNNNN, [anakin@pobox.com])
AC_CONFIG_SRCDIR([midend.c])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
AM_PATH_GTK_2_0([2.0.0])

if test "x$GCC" = "xyes"; then
  AC_MSG_CHECKING([for usable gcc warning flags])
  gccwarningflags=
  for flag in -Wall -Werror -ansi -pedantic; do
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS$gccwarningflags $flag $GTK_CFLAGS"
    LIBS="$GTK_LIBS $LIBS"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
        #include <stdio.h>
        #include <assert.h>
        #include <stdlib.h>
        #include <time.h>
        #include <stdarg.h>
        #include <string.h>
        #include <errno.h>
        #include <math.h>

        #include <sys/time.h>
        #include <sys/resource.h>

        #include <gtk/gtk.h>
        #include <gdk/gdkkeysyms.h>

        #include <gdk-pixbuf/gdk-pixbuf.h>

        #include <gdk/gdkx.h>
        #include <X11/Xlib.h>
        #include <X11/Xutil.h>
        #include <X11/Xatom.h>
    ],[
        return 0;
    ])], [gccwarningflags="$gccwarningflags $flag"], [])
    CFLAGS="$ac_save_CFLAGS"
    LIBS="$ac_save_LIBS"
  done
  AC_MSG_RESULT($gccwarningflags)
  CFLAGS="$CFLAGS$gccwarningflags"
fi

AC_PROG_RANLIB
AC_PROG_INSTALL
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
