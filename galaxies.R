# -*- makefile -*-

GALAXIES = galaxies dsf

galaxies : [X] GTK COMMON GALAXIES galaxies-icon|no-icon

galaxies : [G] WINDOWS COMMON GALAXIES galaxies.res|noicon.res

galaxiessolver : [U] galaxies[STANDALONE_SOLVER] dsf STANDALONE m.lib
galaxiessolver : [C] galaxies[STANDALONE_SOLVER] dsf STANDALONE

galaxiespicture : [U] galaxies[STANDALONE_PICTURE_GENERATOR] dsf STANDALONE
                + m.lib
galaxiespicture : [C] galaxies[STANDALONE_PICTURE_GENERATOR] dsf STANDALONE

ALL += galaxies

!begin gtk
GAMES += galaxies
!end

!begin >list.c
    A(galaxies) \
!end

!begin >wingames.lst
galaxies.exe:Galaxies
!end
