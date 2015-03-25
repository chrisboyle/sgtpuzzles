# -*- makefile -*-

FILLING_EXTRA = dsf

fillingsolver :	[U] filling[STANDALONE_SOLVER] FILLING_EXTRA STANDALONE
fillingsolver :	[C] filling[STANDALONE_SOLVER] FILLING_EXTRA STANDALONE

filling : [X] GTK COMMON filling FILLING_EXTRA filling-icon|no-icon

filling : [G] WINDOWS COMMON filling FILLING_EXTRA filling.res|noicon.res

ALL += filling[COMBINED] FILLING_EXTRA

!begin am gtk
GAMES += filling
!end

!begin >list.c
    A(filling) \
!end

!begin >gamedesc.txt
filling:filling.exe:Filling:Polyomino puzzle:Mark every square with the area of its containing region.
!end
