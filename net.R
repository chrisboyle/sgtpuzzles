# -*- makefile -*-

NET      = net tree234 dsf

net      : [X] GTK COMMON NET net-icon|no-icon

# The Windows Net shouldn't be called `net.exe' since Windows
# already has a reasonably important utility program by that name!
netgame  : [G] WINDOWS COMMON NET net.res?

ALL += NET

!begin gtk
GAMES += net
!end

!begin >list.c
    A(net) \
!end

!begin >wingames.lst
netgame.exe
!end
