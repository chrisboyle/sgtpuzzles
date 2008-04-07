# -*- makefile -*-

SOLO     = solo divvy dsf

solo     : [X] GTK COMMON SOLO solo-icon|no-icon

solo     : [G] WINDOWS COMMON SOLO solo.res|noicon.res

solosolver :    [U] solo[STANDALONE_SOLVER] divvy dsf STANDALONE
solosolver :    [C] solo[STANDALONE_SOLVER] divvy dsf STANDALONE

ALL += SOLO

!begin gtk
GAMES += solo
!end

!begin >list.c
    A(solo) \
!end

!begin >wingames.lst
solo.exe:Solo
!end
