# -*- makefile -*-

PEARL    = pearl dsf

pearl    : [X] GTK COMMON PEARL

pearl    : [G] WINDOWS COMMON PEARL

ALL += PEARL

!begin gtk
GAMES += pearl
!end

!begin >list.c
    A(pearl) \
!end
