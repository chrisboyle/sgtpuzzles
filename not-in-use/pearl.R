# -*- makefile -*-

PEARL_EXTRA    = dsf tree234 grid penrose loopgen tdq

pearl          : [X] GTK COMMON pearl PEARL_EXTRA pearl-icon|no-icon
pearl          : [G] WINDOWS COMMON pearl PEARL_EXTRA pearl.res?

pearlbench     : [U] pearl[STANDALONE_SOLVER] PEARL_EXTRA STANDALONE m.lib
pearlbench     : [C] pearl[STANDALONE_SOLVER] PEARL_EXTRA STANDALONE

ALL += pearl[COMBINED] PEARL_EXTRA

!begin am gtk
GAMES += pearl
!end

!begin >list.c
    A(pearl) \
!end

!begin >gamedesc.txt
pearl:pearl.exe:Pearl:Loop-drawing puzzle:Draw a single closed loop, given clues about corner and straight squares.
!end
