# -*- makefile -*-

FILLING = filling dsf filling-icon|no-icon

fillingsolver :	[U] filling[STANDALONE_SOLVER] dsf STANDALONE
fillingsolver :	[C] filling[STANDALONE_SOLVER] dsf STANDALONE

filling : [X] GTK COMMON FILLING

filling : [G] WINDOWS COMMON FILLING

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
