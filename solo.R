# -*- makefile -*-

solo     : [X] GTK COMMON solo

solo     : [G] WINDOWS COMMON solo

solosolver :    [U] solo[STANDALONE_SOLVER] STANDALONE
solosolver :    [C] solo[STANDALONE_SOLVER] STANDALONE

ALL += solo

!begin gtk
GAMES += solo
!end

!begin >list.c
    A(solo) \
!end
