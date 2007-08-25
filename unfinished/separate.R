# -*- makefile -*-

SEPARATE = separate divvy dsf

separate : [X] GTK COMMON SEPARATE separate-icon|no-icon

separate : [G] WINDOWS COMMON SEPARATE separate.res|noicon.res

ALL += separate

!begin gtk
GAMES += separate
!end

!begin >list.c
    A(separate) \
!end

!begin >wingames.lst
separate.exe:Separate
!end
