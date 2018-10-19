# -*- makefile -*-

KEEN_EXTRA        = dsf LATIN
KEEN_EXTRA_SOLVER = dsf LATIN_SOLVER

keen    : [X] GTK COMMON keen KEEN_EXTRA keen-icon|no-icon

keen    : [G] WINDOWS COMMON keen KEEN_EXTRA keen.res|noicon.res

keensolver : [U] keen[STANDALONE_SOLVER] KEEN_EXTRA_SOLVER STANDALONE
keensolver : [C] keen[STANDALONE_SOLVER] KEEN_EXTRA_SOLVER STANDALONE

ALL += keen[COMBINED] KEEN_EXTRA

!begin am gtk
GAMES += keen
!end

!begin >list.c
    A(keen) \
!end

!begin >gamedesc.txt
keen:keen.exe:Keen:Arithmetic Latin square puzzle:Complete the latin square in accordance with the arithmetic clues.
!end
