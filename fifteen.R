# -*- makefile -*-

fifteen  : [X] GTK COMMON fifteen fifteen-icon|no-icon

fifteen  : [G] WINDOWS COMMON fifteen fifteen.res|noicon.res

ALL += fifteen

!begin gtk
GAMES += fifteen
!end

!begin >list.c
    A(fifteen) \
!end

!begin >wingames.lst
fifteen.exe:Fifteen
!end
