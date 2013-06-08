# -*- makefile -*-

unruly : [X] GTK COMMON unruly unruly-icon|no-icon
unruly : [G] WINDOWS COMMON unruly unruly.res|noicon.res

unrulysolver : [U] unruly[STANDALONE_SOLVER] STANDALONE
unrulysolver : [C] unruly[STANDALONE_SOLVER] STANDALONE

ALL += unruly[COMBINED]

!begin gtk
GAMES += unruly
!end

!begin >list.c
    A(unruly) \
!end

!begin >gamedesc.txt
unruly:unruly.exe:Unruly:Black and white grid puzzle
!end
