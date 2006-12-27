# -*- makefile -*-

pattern  : [X] GTK COMMON pattern

pattern  : [G] WINDOWS COMMON pattern pattern.res?

patternsolver : [U] pattern[STANDALONE_SOLVER] STANDALONE
patternsolver : [C] pattern[STANDALONE_SOLVER] STANDALONE

ALL += pattern

!begin gtk
GAMES += pattern
!end

!begin >list.c
    A(pattern) \
!end
