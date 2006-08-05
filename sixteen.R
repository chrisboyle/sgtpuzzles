# -*- makefile -*-

sixteen  : [X] GTK COMMON sixteen

sixteen  : [G] WINDOWS COMMON sixteen

ALL += sixteen

!begin gtk
GAMES += sixteen
!end

!begin >list.c
    A(sixteen) \
!end
