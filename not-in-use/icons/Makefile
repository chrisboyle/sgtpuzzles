# Makefile for Puzzles icons.

PUZZLES = blackbox bridges cube dominosa fifteen filling flip flood	\
	  galaxies guess inertia keen lightup loopy magnets map mines	\
	  net netslide palisade pattern pearl pegs range rect		\
	  samegame signpost singles sixteen slant solo tents towers	\
	  twiddle tracks undead unequal unruly untangle

BINARIES = $(patsubst %,../%,$(PUZZLES))
BASE = $(patsubst %,%-base.png,$(PUZZLES))
WEB = $(patsubst %,%-web.png,$(PUZZLES))

IBASE = $(patsubst %,%-ibase.png,$(PUZZLES))
IBASE4 = $(patsubst %,%-ibase4.png,$(PUZZLES))
P64D24 = $(patsubst %,%-64d24.png,$(PUZZLES))
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
CICONS = $(patsubst %,%-icon.c,$(PUZZLES))
RC = $(patsubst %,%.rc,$(PUZZLES))
ANDROIDICONS = $(patsubst %,../../res/drawable/%.png,$(PUZZLES))

BIN = ../
PIC = ./

# Work around newer ImageMagick unilaterally distorting colours when
# converting to PNG.
CSP = -set colorspace RGB

base: $(BASE)
web: $(WEB)
pngicons: $(P64D24) $(P48D24) $(P32D24) $(P16D24)
winicons: $(ICONS) $(RC)
gtkicons: $(CICONS)
androidicons: $(ANDROIDICONS) ../../res/drawable/icon.png
../../res/drawable/icon.png: ../../res/drawable/net.png
	cp ../../res/drawable/net.png ../../res/drawable/icon.png
all: base web pngicons winicons gtkicons

# Make sure puzzle binaries are built with the right rules
$(BINARIES): ../%:
	@echo "*** Building $@ GTK version to create screenshot: if you can't do this, you can instead (cd icons && make) on another machine"
	$(MAKE) -C .. $*

# Build the base puzzle screenshots from which all the other images
# are derived. Some of them involve showing a move animation
# part-way through.
fifteen-base.png : override REDO=0.3
flip-base.png : override REDO=0.3
netslide-base.png : override REDO=0.3
sixteen-base.png : override REDO=0.3
twiddle-base.png : override REDO=0.3
$(BASE): %-base.png: $(BIN)% $(PIC)%.sav
	$(PIC)screenshot.sh $(BIN)$* $(PIC)$*.sav $@ $(REDO)

# Build the screenshots for the web, by scaling the original base
# images to a uniform size.
$(WEB): %-web.png: %-base.png
	$(PIC)square.pl 150 5 $^ $@

# Build the base _icon_ images, by careful cropping of the base
# images: icons are very small so it's often necessary to zoom in
# on a smaller portion of the screenshot.
blackbox-ibase.png : override CROP=352x352 144x144+0+208
bridges-ibase.png : override CROP=264x264 107x107+157+157
dominosa-ibase.png : override CROP=304x272 152x152+152+0
fifteen-ibase.png : override CROP=240x240 120x120+0+120
filling-ibase.png : override CROP=256x256 133x133+14+78
flip-ibase.png : override CROP=288x288 145x145+120+72
galaxies-ibase.png : override CROP=288x288 165x165+0+0
guess-ibase.png : override CROP=263x420 178x178+75+17
inertia-ibase.png : override CROP=321x321 128x128+193+0
keen-ibase.png : override CROP=288x288 96x96+24+120
lightup-ibase.png : override CROP=256x256 112x112+144+0
loopy-ibase.png : override CROP=257x257 113x113+0+0
magnets-ibase.png : override CROP=264x232 96x96+36+100
mines-ibase.png : override CROP=240x240 110x110+130+130
net-ibase.png : override CROP=193x193 113x113+0+80
netslide-ibase.png : override CROP=289x289 144x144+0+0
palisade-ibase.png : override CROP=288x288 192x192+0+0
pattern-ibase.png : override CROP=384x384 223x223+0+0
pearl-ibase.png : override CROP=216x216 94x94+108+15
pegs-ibase.png : override CROP=263x263 147x147+116+0
range-ibase.png : override CROP=256x256 98x98+111+15
rect-ibase.png : override CROP=205x205 115x115+90+0
signpost-ibase.png : override CROP=240x240 98x98+23+23
singles-ibase.png : override CROP=224x224 98x98+15+15
sixteen-ibase.png : override CROP=288x288 144x144+144+144
slant-ibase.png : override CROP=321x321 160x160+160+160
solo-ibase.png : override CROP=481x481 145x145+24+24
tents-ibase.png : override CROP=320x320 165x165+142+0
towers-ibase.png : override CROP=300x300 102x102+151+6
tracks-ibase.png : override CROP=246x246 118x118+6+6
twiddle-ibase.png : override CROP=192x192 102x102+69+21
undead-ibase.png : override CROP=416x480 192x192+16+80
unequal-ibase.png : override CROP=208x208 104x104+104+104
untangle-ibase.png : override CROP=320x320 164x164+3+116
$(IBASE): %-ibase.png: %-base.png
	$(PIC)crop.sh $^ $@ $(CROP)

# Convert the full-size icon images to 4-bit colour, because that
# seems to work better than reducing it in 24 bits and then
# dithering.
$(IBASE4): %-ibase4.png: %-ibase.png
	convert -colors 16 +dither $(CSP) -map $(PIC)win16pal.xpm $^ $@

# Build the 24-bit PNGs for the icons, at three sizes.
$(P64D24): %-64d24.png: %-ibase.png
	$(PIC)square.pl 64 4 $^ $@
$(P48D24): %-48d24.png: %-ibase.png
	$(PIC)square.pl 48 4 $^ $@
$(P32D24): %-32d24.png: %-ibase.png
	$(PIC)square.pl 32 2 $^ $@
$(P16D24): %-16d24.png: %-ibase.png
	$(PIC)square.pl 16 1 $^ $@
$(ANDROIDICONS): ../../res/drawable/%.png: %-64d24.png
	cp $^ $@

# The 8-bit icon PNGs are just custom-paletted quantisations of the
# 24-bit ones.
$(P48D8) $(P32D8) $(P16D8): %d8.png: %d24.png
	convert -colors 256 $^ $@

# But the depth-4 images work better if we re-shrink from the
# ibase4 versions of the images, and then normalise the colours
# again afterwards. (They're still not very good, but my hope is
# that on most modern Windows machines this won't matter too
# much...)
$(P48D4): %-48d4.png: %-ibase4.png
	$(PIC)square.pl 48 1 $^ $@-tmp2.png
	convert -colors 16 $(CSP) -map $(PIC)win16pal.xpm $@-tmp2.png $@
	rm -f $@-tmp2.png
$(P32D4): %-32d4.png: %-ibase.png
	$(PIC)square.pl 32 1 $^ $@-tmp2.png
	convert -colors 16 $(CSP) -map $(PIC)win16pal.xpm $@-tmp2.png $@
	rm -f $@-tmp2.png
$(P16D4): %-16d4.png: %-ibase.png
	$(PIC)square.pl 16 1 $^ $@-tmp2.png
	convert -colors 16 $(CSP) -map $(PIC)win16pal.xpm $@-tmp2.png $@
	rm -f $@-tmp2.png

# Build the actual Windows icons themselves, by feeding all those
# PNGs to my icon builder script.
$(ICONS): %.ico: %-48d24.png %-48d8.png %-48d4.png \
                 %-32d24.png %-32d8.png %-32d4.png \
                 %-16d24.png %-16d8.png %-16d4.png
	$(PIC)icon.pl -24 $*-48d24.png $*-32d24.png $*-16d24.png \
		      -8  $*-48d8.png  $*-32d8.png  $*-16d8.png  \
		      -4  $*-48d4.png  $*-32d4.png  $*-16d4.png  > $@

# Build the .RC files which bind the icons into the applications.
$(RC): %.rc:
	echo '#include "puzzles.rc2"' > $@
	echo '200 ICON "$*.ico"' >> $@

# Build the GTK icon source files.
$(CICONS): %-icon.c: %-16d24.png %-32d24.png %-48d24.png
	$(PIC)cicon.pl $^ > $@	

clean:
	rm -f *.png *.ico *.rc *-icon.c
