# -*- makefile -*-

TENTS    = tents maxflow

tents    : [X] GTK COMMON TENTS

tents    : [G] WINDOWS COMMON TENTS

ALL += TENTS

tentssolver :   [U] tents[STANDALONE_SOLVER] maxflow STANDALONE
tentssolver :   [C] tents[STANDALONE_SOLVER] maxflow STANDALONE

!begin gtk
GAMES += tents
!end

!begin >list.c
    A(tents) \
!end
