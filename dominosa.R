# -*- makefile -*-

dominosa : [X] GTK COMMON dominosa

dominosa : [G] WINDOWS COMMON dominosa

ALL += dominosa

!begin gtk
GAMES += dominosa
!end

!begin >list.c
    A(dominosa) \
!end
