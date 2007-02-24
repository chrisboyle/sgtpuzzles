# -*- makefile -*-

rect     : [X] GTK COMMON rect rect-icon|no-icon

rect     : [G] WINDOWS COMMON rect rect.res?

ALL += rect

!begin gtk
GAMES += rect
!end

!begin >list.c
    A(rect) \
!end

!begin >wingames.lst
rect.exe:Rectangles
!end
