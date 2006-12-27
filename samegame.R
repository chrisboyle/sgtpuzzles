# -*- makefile -*-

samegame : [X] GTK COMMON samegame

samegame : [G] WINDOWS COMMON samegame samegame.res?

ALL += samegame

!begin gtk
GAMES += samegame
!end

!begin >list.c
    A(samegame) \
!end
