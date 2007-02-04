# -*- makefile -*-

blackbox : [X] GTK COMMON blackbox blackbox-icon|no-icon

blackbox : [G] WINDOWS COMMON blackbox blackbox.res?

ALL += blackbox

!begin gtk
GAMES += blackbox
!end

!begin >list.c
    A(blackbox) \
!end

!begin >wingames.lst
blackbox.exe
!end
