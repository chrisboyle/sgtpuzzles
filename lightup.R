# -*- makefile -*-

LIGHTUP  = lightup combi

lightup  : [X] GTK COMMON LIGHTUP lightup-icon|no-icon

lightup  : [G] WINDOWS COMMON LIGHTUP lightup.res?

lightupsolver : [U] lightup[STANDALONE_SOLVER] combi STANDALONE
lightupsolver : [C] lightup[STANDALONE_SOLVER] combi STANDALONE

ALL += LIGHTUP

!begin gtk
GAMES += lightup
!end

!begin >list.c
    A(lightup) \
!end

!begin >wingames.lst
lightup.exe:Light Up
!end
