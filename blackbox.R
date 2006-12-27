# -*- makefile -*-

blackbox : [X] GTK COMMON blackbox

blackbox : [G] WINDOWS COMMON blackbox blackbox.res?

ALL += blackbox

!begin gtk
GAMES += blackbox
!end

!begin >list.c
    A(blackbox) \
!end
