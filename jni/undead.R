# -*- makefile -*-

undead : [X] GTK COMMON undead undead-icon|no-icon
undead : [G] WINDOWS COMMON undead undead.res|noicon.res

ALL += undead[COMBINED]

!begin am gtk
GAMES += undead
!end

!begin >list.c
    A(undead) \
!end

!begin >gamedesc.txt
undead:undead.exe:Undead:Monster-placing puzzle
!end
