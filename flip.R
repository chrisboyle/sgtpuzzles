# -*- makefile -*-

FLIP_EXTRA = tree234

flip     : [X] GTK COMMON flip FLIP_EXTRA flip-icon|no-icon

flip     : [G] WINDOWS COMMON flip FLIP_EXTRA flip.res|noicon.res

ALL += flip[COMBINED] FLIP_EXTRA

!begin gtk
GAMES += flip
!end

!begin >list.c
    A(flip) \
!end

!begin >wingames.lst
flip.exe:Flip
!end
