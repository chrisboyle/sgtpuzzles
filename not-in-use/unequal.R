# -*- makefile -*-

UNEQUAL_EXTRA        = LATIN
UNEQUAL_EXTRA_SOLVER = LATIN_SOLVER

unequal  : [X] GTK COMMON unequal UNEQUAL_EXTRA unequal-icon|no-icon

unequal  : [G] WINDOWS COMMON unequal UNEQUAL_EXTRA unequal.res|noicon.res

unequalsolver : [U] unequal[STANDALONE_SOLVER] UNEQUAL_EXTRA_SOLVER STANDALONE
unequalsolver : [C] unequal[STANDALONE_SOLVER] UNEQUAL_EXTRA_SOLVER STANDALONE

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
