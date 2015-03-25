# -*- makefile -*-

SLANT_EXTRA = dsf

slant    : [X] GTK COMMON slant SLANT_EXTRA slant-icon|no-icon

slant    : [G] WINDOWS COMMON slant SLANT_EXTRA slant.res|noicon.res

slantsolver :   [U] slant[STANDALONE_SOLVER] SLANT_EXTRA STANDALONE
slantsolver :   [C] slant[STANDALONE_SOLVER] SLANT_EXTRA STANDALONE

ALL += slant[COMBINED] SLANT_EXTRA

!begin am gtk
GAMES += slant
!end

!begin >list.c
    A(slant) \
!end

!begin >gamedesc.txt
slant:slant.exe:Slant:Maze-drawing puzzle:Draw a maze of slanting lines that matches the clues.
!end
