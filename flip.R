# -*- makefile -*-

FLIP     = flip tree234

flip     : [X] GTK COMMON FLIP

flip     : [G] WINDOWS COMMON FLIP

ALL += FLIP

!begin gtk
GAMES += flip
!end

!begin >list.c
    A(flip) \
!end
