# -*- makefile -*-

UNEQUAL  = unequal latin tree234 maxflow

unequal  : [X] GTK COMMON UNEQUAL unequal-icon|no-icon

unequal  : [G] WINDOWS COMMON UNEQUAL unequal.res?

unequalsolver : [U] unequal[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] tree234 maxflow STANDALONE
unequalsolver : [C] unequal[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] tree234 maxflow STANDALONE

latincheck : [U] latin[STANDALONE_LATIN_TEST] tree234 maxflow STANDALONE
latincheck : [C] latin[STANDALONE_LATIN_TEST] tree234 maxflow STANDALONE

ALL += UNEQUAL

!begin gtk
GAMES += unequal
!end

!begin >list.c
    A(unequal) \
!end
