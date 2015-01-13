# -*- makefile -*-

sixteen  : [X] GTK COMMON sixteen sixteen-icon|no-icon

sixteen  : [G] WINDOWS COMMON sixteen sixteen.res|noicon.res

ALL += sixteen[COMBINED]

!begin am gtk
GAMES += sixteen
!end

!begin >list.c
    A(sixteen) \
!end

!begin >gamedesc.txt
sixteen:sixteen.exe:Sixteen:Toroidal sliding block puzzle:Slide a row at a time to arrange the tiles into order.
!end
