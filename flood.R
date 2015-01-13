# -*- makefile -*-

flood     : [X] GTK COMMON flood flood-icon|no-icon

flood     : [G] WINDOWS COMMON flood flood.res|noicon.res

ALL += flood[COMBINED]

!begin am gtk
GAMES += flood
!end

!begin >list.c
    A(flood) \
!end

!begin >gamedesc.txt
flood:flood.exe:Flood:Flood-filling puzzle:Turn the grid the same colour in as few flood fills as possible.
!end
