# -*- makefile -*-

SLANT    = slant dsf

slant    : [X] GTK COMMON SLANT

slant    : [G] WINDOWS COMMON SLANT slant.res?

slantsolver :   [U] slant[STANDALONE_SOLVER] dsf STANDALONE
slantsolver :   [C] slant[STANDALONE_SOLVER] dsf STANDALONE

ALL += SLANT

!begin gtk
GAMES += slant
!end

!begin >list.c
    A(slant) \
!end
