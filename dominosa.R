# -*- makefile -*-

DOMINOSA_EXTRA = laydomino dsf sort findloop

dominosa : [X] GTK COMMON dominosa DOMINOSA_EXTRA dominosa-icon|no-icon

dominosa : [G] WINDOWS COMMON dominosa DOMINOSA_EXTRA dominosa.res|noicon.res

ALL += dominosa[COMBINED] DOMINOSA_EXTRA

dominosasolver :   [U] dominosa[STANDALONE_SOLVER] DOMINOSA_EXTRA STANDALONE
dominosasolver :   [C] dominosa[STANDALONE_SOLVER] DOMINOSA_EXTRA STANDALONE

!begin am gtk
GAMES += dominosa
!end

!begin >list.c
    A(dominosa) \
!end

!begin >gamedesc.txt
dominosa:dominosa.exe:Dominosa:Domino tiling puzzle:Tile the rectangle with a full set of dominoes.
!end
