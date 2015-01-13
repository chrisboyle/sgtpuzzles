# -*- makefile -*-

sokoban  : [X] GTK COMMON sokoban sokoban-icon|no-icon

sokoban  : [G] WINDOWS COMMON sokoban sokoban.res?

ALL += sokoban[COMBINED]

!begin am gtk
GAMES += sokoban
!end

!begin >list.c
    A(sokoban) \
!end

!begin >gamedesc.txt
sokoban:sokoban.exe:Sokoban:Barrel-pushing puzzle:Push all the barrels into the target squares.
!end
