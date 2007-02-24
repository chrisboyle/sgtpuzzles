# -*- makefile -*-

guess    : [X] GTK COMMON guess guess-icon|no-icon

guess    : [G] WINDOWS COMMON guess guess.res?

ALL += guess

!begin gtk
GAMES += guess
!end

!begin >list.c
    A(guess) \
!end

!begin >wingames.lst
guess.exe:Guess
!end
