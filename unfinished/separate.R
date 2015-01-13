# -*- makefile -*-

SEPARATE_EXTRA = divvy dsf

separate       : [X] GTK COMMON separate SEPARATE_EXTRA separate-icon|no-icon

separate       : [G] WINDOWS COMMON separate SEPARATE_EXTRA separate.res|noicon.res

ALL += separate[COMBINED] SEPARATE_EXTRA

!begin am gtk
GAMES += separate
!end

!begin >list.c
    A(separate) \
!end

!begin >gamedesc.txt
separate:separate.exe:Separate:Rectangle-dividing puzzle:Partition the grid into regions containing one of each letter.
!end
