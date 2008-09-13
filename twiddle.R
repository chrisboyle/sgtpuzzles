# -*- makefile -*-

twiddle  : [X] GTK COMMON twiddle twiddle-icon|no-icon

twiddle  : [G] WINDOWS COMMON twiddle twiddle.res|noicon.res

ALL += twiddle[COMBINED]

!begin gtk
GAMES += twiddle
!end

!begin >list.c
    A(twiddle) \
!end

!begin >wingames.lst
twiddle.exe:Twiddle
!end
