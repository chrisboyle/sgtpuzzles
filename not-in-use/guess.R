# -*- makefile -*-

guess    : [X] GTK COMMON guess guess-icon|no-icon

guess    : [G] WINDOWS COMMON guess guess.res|noicon.res

ALL += guess[COMBINED]

!begin am gtk
GAMES += guess
!end

!begin >list.c
    A(guess) \
!end

!begin >gamedesc.txt
guess:guess.exe:Guess:Combination-guessing puzzle:Guess the hidden combination of colours.
!end
