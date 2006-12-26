# Makefile for Puzzles icons.

PUZZLES = blackbox bridges cube dominosa fifteen flip guess inertia lightup \
          loopy map mines net netslide pattern pegs rect samegame sixteen \
          slant solo tents twiddle untangle

BASE = $(patsubst %,%-base.png,$(PUZZLES))
WEB = $(patsubst %,%-web.png,$(PUZZLES))

BASE4 = $(patsubst %,%-base4.png,$(PUZZLES))

P48D24 = $(patsubst %,%-48d24.png,$(PUZZLES))
P48D8 = $(patsubst %,%-48d8.png,$(PUZZLES))
P48D4 = $(patsubst %,%-48d4.png,$(PUZZLES))
P32D24 = $(patsubst %,%-32d24.png,$(PUZZLES))
P32D8 = $(patsubst %,%-32d8.png,$(PUZZLES))
P32D4 = $(patsubst %,%-32d4.png,$(PUZZLES))
P16D24 = $(patsubst %,%-16d24.png,$(PUZZLES))
P16D8 = $(patsubst %,%-16d8.png,$(PUZZLES))
P16D4 = $(patsubst %,%-16d4.png,$(PUZZLES))
ICONS = $(patsubst %,%.ico,$(PUZZLES))

BIN = ../
PIC = ./

base: $(BASE)
web: $(WEB)
pngicons: $(I48D24) $(I32D24) $(I16D24)
icons: $(ICONS)

fifteen-base.png : override REDO=0.3
flip-base.png : override REDO=0.3
netslide-base.png : override REDO=0.3
sixteen-base.png : override REDO=0.3
twiddle-base.png : override REDO=0.3

$(BASE): %-base.png: $(BIN)% $(PIC)%.sav
	$(PIC)screenshot.sh $(BIN)$* $(PIC)$*.sav $@ $(REDO)

$(BASE4): %-base4.png: %-base.png
	convert -colors 16 +dither -map win16pal.xpm $^ $@

$(WEB): %-web.png: %-base.png
	$(PIC)square.pl 150 5 $^ $@

$(P48D24): %-48d24.png: %-base.png
	$(PIC)square.pl 48 4 $^ $@
$(P32D24): %-32d24.png: %-base.png
	$(PIC)square.pl 32 2 $^ $@
$(P16D24): %-16d24.png: %-base.png
	$(PIC)square.pl 16 1 $^ $@

$(P48D8) $(P32D8) $(P16D8): %d8.png: %d24.png
	convert -colors 256 $^ $@

# The depth-4 images work better if we normalise the colours
# _before_ shrinking, and then normalise again afterwards.
$(P48D4): %-48d4.png: %-base4.png
	$(PIC)square.pl 48 1 $^ tmp2.png
	convert -colors 16 -map win16pal.xpm tmp2.png $@
	rm -f tmp.png tmp2.png
$(P32D4): %-32d4.png: %-base.png
	$(PIC)square.pl 32 1 $^ tmp2.png
	convert -colors 16 -map win16pal.xpm tmp2.png $@
	rm -f tmp.png tmp2.png
$(P16D4): %-16d4.png: %-base.png
	$(PIC)square.pl 16 1 $^ tmp2.png
	convert -colors 16 -map win16pal.xpm tmp2.png $@
	rm -f tmp.png tmp2.png

$(ICONS): %.ico: %-48d24.png %-48d8.png %-48d4.png \
                 %-32d24.png %-32d8.png %-32d4.png \
                 %-16d24.png %-16d8.png %-16d4.png
	$(PIC)icon.pl $? > $@

clean:
	rm -f *.png *.ico
