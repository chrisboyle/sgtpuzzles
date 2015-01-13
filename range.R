# -*- makefile -*-

RANGE_EXTRA = dsf

range    : [X] GTK COMMON range RANGE_EXTRA range-icon|no-icon

range    : [G] WINDOWS COMMON range RANGE_EXTRA range.res|noicon.res

ALL += range[COMBINED] RANGE_EXTRA

!begin am gtk
GAMES += range
!end

!begin >list.c
    A(range) \
!end

!begin >gamedesc.txt
range:range.exe:Range:Visible-distance puzzle:Place black squares to limit the visible distance from each numbered cell.
!end
