# -*- makefile -*-

rect     : [X] GTK COMMON rect rect-icon|no-icon

rect     : [G] WINDOWS COMMON rect rect.res|noicon.res

ALL += rect[COMBINED]

!begin am gtk
GAMES += rect
!end

!begin >list.c
    A(rect) \
!end

!begin >gamedesc.txt
rect:rect.exe:Rectangles:Rectangles puzzle:Divide the grid into rectangles with areas equal to the numbers.
!end
