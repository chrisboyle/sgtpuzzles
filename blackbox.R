# -*- makefile -*-

blackbox : [X] GTK COMMON blackbox

blackbox : [G] WINDOWS COMMON blackbox

ALL += blackbox

!begin gtk
GAMES += blackbox
!end

!begin >list.c
    A(blackbox) \
!end
