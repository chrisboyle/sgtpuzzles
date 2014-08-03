# -*- makefile -*-

blackbox : [X] GTK COMMON blackbox blackbox-icon|no-icon

blackbox : [G] WINDOWS COMMON blackbox blackbox.res|noicon.res

ALL += blackbox[COMBINED]

!begin am gtk
GAMES += blackbox
!end

!begin >list.c
    A(blackbox) \
!end

!begin >gamedesc.txt
blackbox:blackbox.exe:Black Box:Ball-finding puzzle
!end
