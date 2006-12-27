# -*- makefile -*-

UNTANGLE = untangle tree234

untangle : [X] GTK COMMON UNTANGLE untangle-icon|no-icon

untangle : [G] WINDOWS COMMON UNTANGLE untangle.res?

ALL += UNTANGLE

!begin gtk
GAMES += untangle
!end

!begin >list.c
    A(untangle) \
!end
