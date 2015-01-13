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
undead:undead.exe:Undead:Monster-placing puzzle:Place ghosts, vampires and zombies so that the right numbers of them can be seen in mirrors.
!end
