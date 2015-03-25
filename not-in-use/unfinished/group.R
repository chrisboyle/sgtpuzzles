# -*- makefile -*-

GROUP_LATIN_EXTRA = tree234 maxflow
GROUP_EXTRA = latin GROUP_LATIN_EXTRA

group    : [X] GTK COMMON group GROUP_EXTRA group-icon|no-icon

group    : [G] WINDOWS COMMON group GROUP_EXTRA group.res|noicon.res

groupsolver : [U] group[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] GROUP_LATIN_EXTRA STANDALONE
groupsolver : [C] group[STANDALONE_SOLVER] latin[STANDALONE_SOLVER] GROUP_LATIN_EXTRA STANDALONE

ALL += group[COMBINED] GROUP_EXTRA

!begin am gtk
GAMES += group
!end

!begin >list.c
    A(group) \
!end

!begin >gamedesc.txt
group:group.exe:Group:Group theory puzzle:Complete the unfinished Cayley table of a group.
!end
