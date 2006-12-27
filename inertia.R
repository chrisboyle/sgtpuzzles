# -*- makefile -*-

inertia  : [X] GTK COMMON inertia

inertia  : [G] WINDOWS COMMON inertia inertia.res?

ALL += inertia

!begin gtk
GAMES += inertia
!end

!begin >list.c
    A(inertia) \
!end
