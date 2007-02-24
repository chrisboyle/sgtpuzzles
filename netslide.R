# -*- makefile -*-

NETSLIDE = netslide tree234

netslide : [X] GTK COMMON NETSLIDE netslide-icon|no-icon

netslide : [G] WINDOWS COMMON NETSLIDE netslide.res?

ALL += NETSLIDE

!begin gtk
GAMES += netslide
!end

!begin >list.c
    A(netslide) \
!end

!begin >wingames.lst
netslide.exe:Netslide
!end
