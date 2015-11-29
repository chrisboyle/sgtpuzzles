# -*- makefile -*-

PALISADE_EXTRA = divvy dsf

palisade    : [X] GTK COMMON palisade PALISADE_EXTRA palisade-icon|no-icon

palisade    : [G] WINDOWS COMMON palisade PALISADE_EXTRA palisade.res|noicon.res

ALL += palisade[COMBINED] PALISADE_EXTRA

!begin am gtk
GAMES += palisade
!end

!begin >list.c
    A(palisade) \
!end

!begin >gamedesc.txt
palisade:palisade.exe:Palisade:Grid-division puzzle:Divide the grid into equal-sized areas in accordance with the clues.
!end
