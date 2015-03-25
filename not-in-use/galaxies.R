# -*- makefile -*-

GALAXIES_EXTRA = dsf

galaxies : [X] GTK COMMON galaxies GALAXIES_EXTRA galaxies-icon|no-icon

galaxies : [G] WINDOWS COMMON galaxies GALAXIES_EXTRA galaxies.res|noicon.res

galaxiessolver : [U] galaxies[STANDALONE_SOLVER] GALAXIES_EXTRA STANDALONE m.lib
galaxiessolver : [C] galaxies[STANDALONE_SOLVER] GALAXIES_EXTRA STANDALONE

galaxiespicture : [U] galaxies[STANDALONE_PICTURE_GENERATOR] GALAXIES_EXTRA STANDALONE
                + m.lib
galaxiespicture : [C] galaxies[STANDALONE_PICTURE_GENERATOR] GALAXIES_EXTRA STANDALONE

ALL += galaxies[COMBINED] GALAXIES_EXTRA

!begin am gtk
GAMES += galaxies
!end

!begin >list.c
    A(galaxies) \
!end

!begin >gamedesc.txt
galaxies:galaxies.exe:Galaxies:Symmetric polyomino puzzle:Divide the grid into rotationally symmetric regions each centred on a dot.
!end
