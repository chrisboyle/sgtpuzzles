# -*- makefile -*-

MAP      = map dsf

map      : [X] GTK COMMON MAP map-icon|no-icon

map      : [G] WINDOWS COMMON MAP map.res?

mapsolver :     [U] map[STANDALONE_SOLVER] dsf STANDALONE m.lib
mapsolver :     [C] map[STANDALONE_SOLVER] dsf STANDALONE

ALL += MAP

!begin gtk
GAMES += map
!end

!begin >list.c
    A(map) \
!end

!begin >wingames.lst
map.exe
!end
