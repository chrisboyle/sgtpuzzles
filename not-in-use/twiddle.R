# -*- makefile -*-

twiddle  : [X] GTK COMMON twiddle twiddle-icon|no-icon

twiddle  : [G] WINDOWS COMMON twiddle twiddle.res|noicon.res

ALL += twiddle[COMBINED]

!begin am gtk
GAMES += twiddle
!end

!begin >list.c
    A(twiddle) \
!end

!begin >gamedesc.txt
twiddle:twiddle.exe:Twiddle:Rotational sliding block puzzle
!end
