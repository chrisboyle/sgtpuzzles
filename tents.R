# -*- makefile -*-

TENTS    = tents maxflow

tents    : [X] GTK COMMON TENTS tents-icon|no-icon

tents    : [G] WINDOWS COMMON TENTS tents.res|noicon.res

ALL += TENTS

tentssolver :   [U] tents[STANDALONE_SOLVER] maxflow STANDALONE
tentssolver :   [C] tents[STANDALONE_SOLVER] maxflow STANDALONE

!begin gtk
GAMES += tents
!end

!begin >list.c
    A(tents) \
!end

!begin >wingames.lst
tents.exe:Tents
!end
