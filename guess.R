# -*- makefile -*-

guess    : [X] GTK COMMON guess

guess    : [G] WINDOWS COMMON guess

ALL += guess

!begin gtk
GAMES += guess
!end

!begin >list.c
    A(guess) \
!end
