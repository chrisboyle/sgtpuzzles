# -*- makefile -*-

solo     : [X] GTK COMMON solo solo-icon|no-icon

solo     : [G] WINDOWS COMMON solo solo.res?

solosolver :    [U] solo[STANDALONE_SOLVER] STANDALONE
solosolver :    [C] solo[STANDALONE_SOLVER] STANDALONE

ALL += solo

!begin gtk
GAMES += solo
!end

!begin >list.c
    A(solo) \
!end

!begin >wingames.lst
solo.exe:Solo
!end
