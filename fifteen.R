# -*- makefile -*-

fifteen  : [X] GTK COMMON fifteen fifteen-icon|no-icon

fifteen  : [G] WINDOWS COMMON fifteen fifteen.res|noicon.res

ALL += fifteen[COMBINED]

!begin gtk
GAMES += fifteen
!end

!begin >list.c
    A(fifteen) \
!end

!begin >gamedesc.txt
fifteen:fifteen.exe:Fifteen:Sliding block puzzle
!end
