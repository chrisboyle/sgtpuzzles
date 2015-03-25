# -*- makefile -*-

NET_EXTRA = tree234 dsf

net      : [X] GTK COMMON net NET_EXTRA net-icon|no-icon

# The Windows Net shouldn't be called `net.exe' since Windows
# already has a reasonably important utility program by that name!
netgame  : [G] WINDOWS COMMON net NET_EXTRA net.res|noicon.res

ALL += net[COMBINED] NET_EXTRA

!begin am gtk
GAMES += net
!end

!begin >list.c
    A(net) \
!end

!begin >gamedesc.txt
net:netgame.exe:Net:Network jigsaw puzzle:Rotate each tile to reassemble the network.
!end
