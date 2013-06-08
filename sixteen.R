# -*- makefile -*-

sixteen  : [X] GTK COMMON sixteen sixteen-icon|no-icon

sixteen  : [G] WINDOWS COMMON sixteen sixteen.res|noicon.res

ALL += sixteen[COMBINED]

!begin gtk
GAMES += sixteen
!end

!begin >list.c
    A(sixteen) \
!end

!begin >gamedesc.txt
sixteen:sixteen.exe:Sixteen:Toroidal sliding block puzzle
!end
