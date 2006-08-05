# -*- makefile -*-

LOOPY    = loopy tree234 dsf

loopy    : [X] GTK COMMON LOOPY

loopy    : [G] WINDOWS COMMON LOOPY

ALL += LOOPY

!begin gtk
GAMES += loopy
!end

!begin >list.c
    A(loopy) \
!end
