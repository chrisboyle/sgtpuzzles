# -*- makefile -*-

rect     : [X] GTK COMMON rect

rect     : [G] WINDOWS COMMON rect rect.res?

ALL += rect

!begin gtk
GAMES += rect
!end

!begin >list.c
    A(rect) \
!end
