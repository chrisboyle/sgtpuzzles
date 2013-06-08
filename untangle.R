# -*- makefile -*-

UNTANGLE_EXTRA = tree234

untangle : [X] GTK COMMON untangle UNTANGLE_EXTRA untangle-icon|no-icon

untangle : [G] WINDOWS COMMON untangle UNTANGLE_EXTRA untangle.res|noicon.res

ALL += untangle[COMBINED] UNTANGLE_EXTRA

!begin gtk
GAMES += untangle
!end

!begin >list.c
    A(untangle) \
!end

!begin >gamedesc.txt
untangle:untangle.exe:Untangle:Planar graph layout puzzle
!end
