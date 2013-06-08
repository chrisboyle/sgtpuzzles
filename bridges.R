# -*- makefile -*-

BRIDGES_EXTRA = dsf

bridges  : [X] GTK COMMON bridges BRIDGES_EXTRA bridges-icon|no-icon

bridges  : [G] WINDOWS COMMON bridges BRIDGES_EXTRA bridges.res|noicon.res

ALL += bridges[COMBINED] BRIDGES_EXTRA

!begin gtk
GAMES += bridges
!end

!begin >list.c
    A(bridges) \
!end

!begin >gamedesc.txt
bridges:bridges.exe:Bridges:Bridge-placing puzzle
!end
