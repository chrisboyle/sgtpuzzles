# -*- makefile -*-

LOOPY     = loopy tree234 dsf grid

loopy     : [X] GTK COMMON LOOPY loopy-icon|no-icon

loopy     : [G] WINDOWS COMMON LOOPY loopy.res|noicon.res

ALL += LOOPY

!begin gtk
GAMES += loopy
!end

!begin >list.c
    A(loopy) \
!end

!begin >wingames.lst
loopy.exe:Loopy
!end
