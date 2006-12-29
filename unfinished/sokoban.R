# -*- makefile -*-

sokoban  : [X] GTK COMMON sokoban sokoban-icon|no-icon

sokoban  : [G] WINDOWS COMMON sokoban sokoban.res?

ALL += sokoban

!begin gtk
GAMES += sokoban
!end

!begin >list.c
    A(sokoban) \
!end
