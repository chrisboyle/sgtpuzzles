# -*- makefile -*-

NETSLIDE_EXTRA = tree234

netslide : [X] GTK COMMON netslide NETSLIDE_EXTRA netslide-icon|no-icon

netslide : [G] WINDOWS COMMON netslide NETSLIDE_EXTRA netslide.res|noicon.res

ALL += netslide[COMBINED] NETSLIDE_EXTRA

!begin am gtk
GAMES += netslide
!end

!begin >list.c
    A(netslide) \
!end

!begin >gamedesc.txt
netslide:netslide.exe:Netslide:Toroidal sliding network puzzle:Slide a row at a time to reassemble the network.
!end
