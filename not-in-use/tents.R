# -*- makefile -*-

TENTS_EXTRA = maxflow dsf

tents    : [X] GTK COMMON tents TENTS_EXTRA tents-icon|no-icon

tents    : [G] WINDOWS COMMON tents TENTS_EXTRA tents.res|noicon.res

ALL += tents[COMBINED] TENTS_EXTRA

tentssolver :   [U] tents[STANDALONE_SOLVER] TENTS_EXTRA STANDALONE
tentssolver :   [C] tents[STANDALONE_SOLVER] TENTS_EXTRA STANDALONE

!begin am gtk
GAMES += tents
!end

!begin >list.c
    A(tents) \
!end

!begin >gamedesc.txt
tents:tents.exe:Tents:Tent-placing puzzle:Place a tent next to each tree.
!end
