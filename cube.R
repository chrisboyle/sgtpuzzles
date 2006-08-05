# -*- makefile -*-

cube     : [X] GTK COMMON cube

cube     : [G] WINDOWS COMMON cube

ALL += cube

!begin gtk
GAMES += cube
!end

!begin >list.c
    A(cube) \
!end
