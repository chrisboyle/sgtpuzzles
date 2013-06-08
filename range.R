# -*- makefile -*-

range    : [X] GTK COMMON range range-icon|no-icon

range    : [G] WINDOWS COMMON range range.res|noicon.res

ALL += range[COMBINED]

!begin gtk
GAMES += range
!end

!begin >list.c
    A(range) \
!end

!begin >gamedesc.txt
range:range.exe:Range:Visible-distance puzzle
!end
