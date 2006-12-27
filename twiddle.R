# -*- makefile -*-

twiddle  : [X] GTK COMMON twiddle twiddle-icon|no-icon

twiddle  : [G] WINDOWS COMMON twiddle twiddle.res?

ALL += twiddle

!begin gtk
GAMES += twiddle
!end

!begin >list.c
    A(twiddle) \
!end
