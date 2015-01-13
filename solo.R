# -*- makefile -*-

SOLO_EXTRA = divvy dsf

solo     : [X] GTK COMMON solo SOLO_EXTRA solo-icon|no-icon

solo     : [G] WINDOWS COMMON solo SOLO_EXTRA solo.res|noicon.res

solosolver :    [U] solo[STANDALONE_SOLVER] SOLO_EXTRA STANDALONE
solosolver :    [C] solo[STANDALONE_SOLVER] SOLO_EXTRA STANDALONE

ALL += solo[COMBINED] SOLO_EXTRA

!begin am gtk
GAMES += solo
!end

!begin >list.c
    A(solo) \
!end

!begin >gamedesc.txt
solo:solo.exe:Solo:Number placement puzzle:Fill in the grid so that each row, column and square block contains one of every digit.
!end
