# -*- makefile -*-

inertia  : [X] GTK COMMON inertia inertia-icon|no-icon

inertia  : [G] WINDOWS COMMON inertia inertia.res|noicon.res

ALL += inertia

!begin gtk
GAMES += inertia
!end

!begin >list.c
    A(inertia) \
!end

!begin >wingames.lst
inertia.exe:Inertia
!end
