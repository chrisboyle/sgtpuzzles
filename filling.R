# -*- makefile -*-

FILLING = filling dsf

fillingsolver :	[U] filling[STANDALONE_SOLVER] dsf STANDALONE
fillingsolver :	[C] filling[STANDALONE_SOLVER] dsf STANDALONE

filling : [X] GTK COMMON FILLING filling-icon|no-icon

filling : [G] WINDOWS COMMON FILLING filling.res|noicon.res

ALL += filling

!begin gtk
GAMES += filling
!end

!begin >list.c
    A(filling) \
!end

!begin >wingames.lst
filling.exe:Filling
!end
