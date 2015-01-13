# -*- makefile -*-

UNEQUAL_EXTRA = latin tree234 maxflow

unequal  : [X] GTK COMMON unequal UNEQUAL_EXTRA unequal-icon|no-icon

unequal  : [G] WINDOWS COMMON unequal UNEQUAL_EXTRA unequal.res|noicon.res

unequalsolver : [U] unequal[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] tree234 maxflow STANDALONE
unequalsolver : [C] unequal[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] tree234 maxflow STANDALONE

latincheck : [U] latin[STANDALONE_LATIN_TEST] tree234 maxflow STANDALONE
latincheck : [C] latin[STANDALONE_LATIN_TEST] tree234 maxflow STANDALONE

ALL += unequal[COMBINED] UNEQUAL_EXTRA

!begin am gtk
GAMES += unequal
!end

!begin >list.c
    A(unequal) \
!end

!begin >gamedesc.txt
unequal:unequal.exe:Unequal:Latin square puzzle:Complete the latin square in accordance with the > signs.
!end
