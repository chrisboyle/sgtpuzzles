# -*- makefile -*-

SLIDE_EXTRA    = dsf tree234

slide          : [X] GTK COMMON slide SLIDE_EXTRA slide-icon|no-icon

slide          : [G] WINDOWS COMMON slide SLIDE_EXTRA slide.res|noicon.res

slidesolver :   [U] slide[STANDALONE_SOLVER] SLIDE_EXTRA STANDALONE
slidesolver :   [C] slide[STANDALONE_SOLVER] SLIDE_EXTRA STANDALONE

ALL += slide[COMBINED] SLIDE_EXTRA

!begin am gtk
GAMES += slide
!end

!begin >list.c
    A(slide) \
!end

!begin >gamedesc.txt
slide:slide.exe:Slide:Sliding block puzzle:Slide the blocks to let the key block out.
!end
