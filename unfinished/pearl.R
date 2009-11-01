# -*- makefile -*-

PEARL_EXTRA    = dsf

pearl          : [X] GTK COMMON pearl PEARL_EXTRA pearl-icon|no-icon

pearl          : [G] WINDOWS COMMON pearl PEARL_EXTRA pearl.res?

ALL += pearl[COMBINED] PEARL_EXTRA

!begin gtk
GAMES += pearl
!end

!begin >list.c
    A(pearl) \
!end

!begin >wingames.lst
pearl.exe:Pearl
!end
