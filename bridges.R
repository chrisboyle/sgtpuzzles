# -*- makefile -*-

BRIDGES  = bridges dsf

bridges  : [X] GTK COMMON BRIDGES bridges-icon|no-icon

bridges  : [G] WINDOWS COMMON BRIDGES bridges.res|noicon.res

ALL += BRIDGES

!begin gtk
GAMES += bridges
!end

!begin >list.c
    A(bridges) \
!end

!begin >wingames.lst
bridges.exe:Bridges
!end
