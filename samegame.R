# -*- makefile -*-

samegame : [X] GTK COMMON samegame samegame-icon|no-icon

samegame : [G] WINDOWS COMMON samegame samegame.res|noicon.res

ALL += samegame[COMBINED]

!begin gtk
GAMES += samegame
!end

!begin >list.c
    A(samegame) \
!end

!begin >wingames.lst
samegame.exe:Same Game
!end
