# Makefile for Puzzles icons.

PUZZLES = blackbox bridges cube dominosa fifteen flip guess inertia lightup \
          loopy map mines net netslide pattern pegs rect samegame sixteen \
          slant solo tents twiddle untangle

BASEPNGS = $(patsubst %,%-base.png,$(PUZZLES))
WEBPNGS = $(patsubst %,%-web.png,$(PUZZLES))

BIN = ../
PIC = ./

base: $(BASEPNGS)
webpics: $(WEBPNGS)

fifteen.png : override REDO=0.3
flip.png : override REDO=0.3
netslide.png : override REDO=0.3
sixteen.png : override REDO=0.3
twiddle.png : override REDO=0.3

$(BASEPNGS): %-base.png: $(BIN)% $(PIC)%.sav $(PIC)screenshot.sh
	$(PIC)screenshot.sh $(BIN)$* $(PIC)$*.sav $@ $(REDO)

$(WEBPNGS): %-web.png: %-base.png
	$(PIC)square.pl 150 5 $^ $@

clean:
	rm -f *.png
