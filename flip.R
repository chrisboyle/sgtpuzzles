# -*- makefile -*-

FLIP     = flip tree234

flip     : [X] GTK COMMON FLIP flip-icon|no-icon

flip     : [G] WINDOWS COMMON FLIP flip.res?

ALL += FLIP

!begin gtk
GAMES += flip
!end

!begin >list.c
    A(flip) \
!end

!begin >wingames.lst
flip.exe:Flip
!end
