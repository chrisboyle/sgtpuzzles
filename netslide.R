# -*- makefile -*-

NETSLIDE = netslide tree234

netslide : [X] GTK COMMON NETSLIDE

netslide : [G] WINDOWS COMMON NETSLIDE

ALL += NETSLIDE

!begin gtk
GAMES += netslide
!end

!begin >list.c
    A(netslide) \
!end
