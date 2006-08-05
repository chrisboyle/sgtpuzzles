# -*- makefile -*-

rect     : [X] GTK COMMON rect

rect     : [G] WINDOWS COMMON rect

ALL += rect

!begin gtk
GAMES += rect
!end

!begin >list.c
    A(rect) \
!end
