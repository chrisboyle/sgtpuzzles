# -*- makefile -*-

pattern  : [X] GTK COMMON pattern pattern-icon|no-icon

pattern  : [G] WINDOWS COMMON pattern pattern.res|noicon.res

patternsolver : [U] pattern[STANDALONE_SOLVER] STANDALONE
patternsolver : [C] pattern[STANDALONE_SOLVER] STANDALONE

ALL += pattern[COMBINED]

!begin gtk
GAMES += pattern
!end

!begin >list.c
    A(pattern) \
!end

!begin >gamedesc.txt
pattern:pattern.exe:Pattern:Pattern puzzle
!end
