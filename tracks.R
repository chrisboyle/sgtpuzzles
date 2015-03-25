# -*- makefile -*-

TRACKS_EXTRA = dsf

tracks  : [X] GTK COMMON tracks TRACKS_EXTRA tracks-icon|no-icon

tracks  : [G] WINDOWS COMMON tracks TRACKS_EXTRA tracks.res|noicon.res

ALL += tracks[COMBINED] TRACKS_EXTRA

!begin am gtk
GAMES += tracks
!end

!begin >list.c
    A(tracks) \
!end

!begin >gamedesc.txt
tracks:tracks.exe:Tracks:Path-finding railway track puzzle:Fill in the railway track according to the clues.
!end
