# -*- makefile -*-

fifteen  : [X] GTK COMMON fifteen

fifteen  : [G] WINDOWS COMMON fifteen

ALL += fifteen

!begin gtk
GAMES += fifteen
!end

!begin >list.c
    A(fifteen) \
!end
