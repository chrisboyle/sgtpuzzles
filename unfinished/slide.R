# -*- makefile -*-

SLIDE    = slide dsf tree234

slide    : [X] GTK COMMON SLIDE slide-icon|no-icon

slide    : [G] WINDOWS COMMON SLIDE slide.res|noicon.res

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
