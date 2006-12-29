# -*- makefile -*-

PEARL    = pearl dsf

pearl    : [X] GTK COMMON PEARL pearl-icon|no-icon

pearl    : [G] WINDOWS COMMON PEARL pearl.res?

ALL += PEARL

!begin gtk
GAMES += pearl
!end

!begin >list.c
    A(pearl) \
!end
