# -*- makefile -*-

PEGS     = pegs tree234

pegs     : [X] GTK COMMON PEGS pegs-icon|no-icon

pegs     : [G] WINDOWS COMMON PEGS pegs.res?

ALL += PEGS

!begin gtk
GAMES += pegs
!end

!begin >list.c
    A(pegs) \
!end

!begin >wingames.lst
pegs.exe:Pegs
!end
