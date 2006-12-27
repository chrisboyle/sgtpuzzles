# -*- makefile -*-

sixteen  : [X] GTK COMMON sixteen sixteen-icon|no-icon

sixteen  : [G] WINDOWS COMMON sixteen sixteen.res?

ALL += sixteen

!begin gtk
GAMES += sixteen
!end

!begin >list.c
    A(sixteen) \
!end
