# -*- makefile -*-

MINES    = mines tree234

mines    : [X] GTK COMMON MINES mines-icon|no-icon

mines    : [G] WINDOWS COMMON MINES mines.res?

mineobfusc :    [U] mines[STANDALONE_OBFUSCATOR] tree234 STANDALONE
mineobfusc :    [C] mines[STANDALONE_OBFUSCATOR] tree234 STANDALONE

ALL += MINES

!begin gtk
GAMES += mines
!end

!begin >list.c
    A(mines) \
!end

!begin >wingames.lst
mines.exe
!end
