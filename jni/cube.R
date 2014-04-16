# -*- makefile -*-

cube     : [X] GTK COMMON cube cube-icon|no-icon

cube     : [G] WINDOWS COMMON cube cube.res|noicon.res

ALL += cube[COMBINED]

!begin gtk
GAMES += cube
!end

!begin >list.c
    A(cube) \
!end

!begin >wingames.lst
cube.exe:Cube
!end
