# -*- makefile -*-

samegame : [X] GTK COMMON samegame samegame-icon|no-icon

samegame : [G] WINDOWS COMMON samegame samegame.res|noicon.res

ALL += samegame[COMBINED]

!begin am gtk
GAMES += samegame
!end

!begin >list.c
    A(samegame) \
!end

!begin >gamedesc.txt
samegame:samegame.exe:Same Game:Block-clearing puzzle:Clear the grid by removing touching groups of the same colour squares.
!end
