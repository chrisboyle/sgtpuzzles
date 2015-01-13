# -*- makefile -*-

FLIP_EXTRA = tree234

flip     : [X] GTK COMMON flip FLIP_EXTRA flip-icon|no-icon

flip     : [G] WINDOWS COMMON flip FLIP_EXTRA flip.res|noicon.res

ALL += flip[COMBINED] FLIP_EXTRA

!begin am gtk
GAMES += flip
!end

!begin >list.c
    A(flip) \
!end

!begin >gamedesc.txt
flip:flip.exe:Flip:Tile inversion puzzle:Flip groups of squares to light them all up at once.
!end
