# -*- makefile -*-

SINGLES_EXTRA = dsf latin maxflow tree234

singles : [X] GTK COMMON singles SINGLES_EXTRA singles-icon|no-icon
singles : [G] WINDOWS COMMON singles SINGLES_EXTRA singles.res|noicon.res

ALL += singles[COMBINED] SINGLES_EXTRA

singlessolver : [U] singles[STANDALONE_SOLVER] SINGLES_EXTRA STANDALONE
singlessolver : [C] singles[STANDALONE_SOLVER] SINGLES_EXTRA STANDALONE

!begin am gtk
GAMES += singles
!end

!begin >list.c
    A(singles) \
!end

!begin >gamedesc.txt
singles:singles.exe:Singles:Number-removing puzzle:Black out the right set of duplicate numbers.
!end
