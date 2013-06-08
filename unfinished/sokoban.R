# -*- makefile -*-

sokoban  : [X] GTK COMMON sokoban sokoban-icon|no-icon

sokoban  : [G] WINDOWS COMMON sokoban sokoban.res?

ALL += sokoban[COMBINED]

!begin gtk
GAMES += sokoban
!end

!begin >list.c
    A(sokoban) \
!end

!begin >gamedesc.txt
unfinished/sokoban:sokoban.exe:Sokoban:Barrel-pushing puzzle
!end
