# -*- makefile -*-

twiddle  : [X] GTK COMMON twiddle

twiddle  : [G] WINDOWS COMMON twiddle

ALL += twiddle

!begin gtk
GAMES += twiddle
!end

!begin >list.c
    A(twiddle) \
!end
