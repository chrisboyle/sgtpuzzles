# -*- makefile -*-

cube     : [X] GTK COMMON cube cube-icon|no-icon

cube     : [G] WINDOWS COMMON cube cube.res|noicon.res

ALL += cube[COMBINED]

!begin am gtk
GAMES += cube
!end

!begin >list.c
    A(cube) \
!end

!begin >gamedesc.txt
cube:cube.exe:Cube:Rolling cube puzzle:Pick up all the blue squares by rolling the cube over them.
!end
