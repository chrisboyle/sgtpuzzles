dnl Configure script for the Unix GTK build of puzzles.

AC_INIT([puzzles], rNNNN, [anakin@pobox.com])
AC_CONFIG_SRCDIR([midend.c])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
if test "x$GCC" = "xyes"; then
    : # FIXME: do something interesting enabling as many warning
      # options as possible without breaking system headers.
fi
AM_PATH_GTK_2_0([2.0.0])
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
