# -*- makefile -*-

sokoban  : [X] GTK COMMON sokoban

sokoban  : [G] WINDOWS COMMON sokoban

ALL += sokoban

!begin gtk
GAMES += sokoban
!end

!begin >list.c
    A(sokoban) \
!end
