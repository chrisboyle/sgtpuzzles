# -*- makefile -*-

inertia  : [X] GTK COMMON inertia inertia-icon|no-icon

inertia  : [G] WINDOWS COMMON inertia inertia.res|noicon.res

ALL += inertia[COMBINED]

!begin gtk
GAMES += inertia
!end

!begin >list.c
    A(inertia) \
!end

!begin >gamedesc.txt
inertia:inertia.exe:Inertia:Gem-collecting puzzle
!end
