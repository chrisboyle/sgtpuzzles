# -*- makefile -*-

NETSLIDE = netslide tree234

netslide : [X] GTK COMMON NETSLIDE

netslide : [G] WINDOWS COMMON NETSLIDE netslide.res?

ALL += NETSLIDE

!begin gtk
GAMES += netslide
!end

!begin >list.c
    A(netslide) \
!end
