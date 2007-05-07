# -*- makefile -*-

SLIDE    = slide dsf tree234

slide    : [X] GTK COMMON SLIDE slide-icon|no-icon

slide    : [G] WINDOWS COMMON SLIDE slide.res|noicon.res

slidesolver :   [U] slide[STANDALONE_SOLVER] dsf tree234 STANDALONE
slidesolver :   [C] slide[STANDALONE_SOLVER] dsf tree234 STANDALONE

ALL += SLIDE

!begin gtk
GAMES += slide
!end

!begin >list.c
    A(slide) \
!end

!begin >wingames.lst
slide.exe:Slide
!end
