# -*- makefile -*-

MINES_EXTRA = tree234

mines    : [X] GTK COMMON mines MINES_EXTRA mines-icon|no-icon

mines    : [G] WINDOWS COMMON mines MINES_EXTRA mines.res|noicon.res

mineobfusc :    [U] mines[STANDALONE_OBFUSCATOR] MINES_EXTRA STANDALONE
mineobfusc :    [C] mines[STANDALONE_OBFUSCATOR] MINES_EXTRA STANDALONE

ALL += mines[COMBINED] MINES_EXTRA

!begin am gtk
GAMES += mines
!end

!begin >list.c
    A(mines) \
!end

!begin >gamedesc.txt
mines:mines.exe:Mines:Mine-finding puzzle:Find all the mines without treading on any of them.
!end
