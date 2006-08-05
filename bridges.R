# -*- makefile -*-

BRIDGES  = bridges dsf

bridges  : [X] GTK COMMON BRIDGES

bridges  : [G] WINDOWS COMMON BRIDGES

ALL += BRIDGES

!begin gtk
GAMES += bridges
!end

!begin >list.c
    A(bridges) \
!end
