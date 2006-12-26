#!/usr/bin/perl 

# Take nine input image files and convert them into a
# multi-resolution Windows .ICO icon file. The nine files should
# be, in order:
#
#  - 48x48 icons at 24-bit, 8-bit and 4-bit colour depth respectively
#  - 32x32 icons at 24-bit, 8-bit and 4-bit colour depth respectively
#  - 16x16 icons at 24-bit, 8-bit and 4-bit colour depth respectively
#
# ICO files support a 1-bit alpha channel on all these image types.
#
# TODO: it would be nice if we could extend this icon builder to
# support monochrome icons and a user-specified subset of the
# available formats. None of that should be too hard: the
# monochrome raster data has the same format as the alpha channel,
# monochrome images have a 2-colour palette containing 000000 and
# FFFFFF respectively, and really the biggest problem is designing
# a sensible command-line syntax!

%win16pal = (
    "\x00\x00\x00\x00" => 0,
    "\x00\x00\x80\x00" => 1,
    "\x00\x80\x00\x00" => 2,
    "\x00\x80\x80\x00" => 3,
    "\x80\x00\x00\x00" => 4,
    "\x80\x00\x80\x00" => 5,
    "\x80\x80\x00\x00" => 6,
    "\xC0\xC0\xC0\x00" => 7,
    "\x80\x80\x80\x00" => 8,
    "\x00\x00\xFF\x00" => 9,
    "\x00\xFF\x00\x00" => 10,
    "\x00\xFF\xFF\x00" => 11,
    "\xFF\x00\x00\x00" => 12,
    "\xFF\x00\xFF\x00" => 13,
    "\xFF\xFF\x00\x00" => 14,
    "\xFF\xFF\xFF\x00" => 15,
);
@win16pal = sort { $win16pal{$a} <=> $win16pal{$b} } keys %win16pal;

@hdr = ();
@dat = ();

&readicon($ARGV[0], 48, 48, 24);
&readicon($ARGV[1], 48, 48, 8);
&readicon($ARGV[2], 48, 48, 4);
&readicon($ARGV[3], 32, 32, 24);
&readicon($ARGV[4], 32, 32, 8);
&readicon($ARGV[5], 32, 32, 4);
&readicon($ARGV[6], 16, 16, 24);
&readicon($ARGV[7], 16, 16, 8);
&readicon($ARGV[8], 16, 16, 4);

# Now write out the output icon file.
print pack "vvv", 0, 1, scalar @hdr; # file-level header
$filepos = 6 + 16 * scalar @hdr;
for ($i = 0; $i < scalar @hdr; $i++) {
    print $hdr[$i];
    print pack "V", $filepos;
    $filepos += length($dat[$i]);
}
for ($i = 0; $i < scalar @hdr; $i++) {
    print $dat[$i];
}

sub readicon {
    my $filename = shift @_;
    my $w = shift @_;
    my $h = shift @_;
    my $depth = shift @_;
    my $pix;
    my $i;
    my %pal;

    # Read the file in as RGBA data. We flip vertically at this
    # point, to avoid having to do it ourselves (.BMP and hence
    # .ICO are bottom-up).
    my $data = [];
    open IDATA, "convert -flip -depth 8 $filename rgba:- |";
    push @$data, $rgb while (read IDATA,$rgb,4,0) == 4;
    close IDATA;
    # Check we have the right amount of data.
    $xl = $w * $h;
    $al = scalar @$data;
    die "wrong amount of image data ($al, expected $xl) from $filename\n"
      unless $al == $xl;

    # Build the alpha channel now, so we can exclude transparent
    # pixels from the palette analysis. We replace transparent
    # pixels with undef in the data array.
    #
    # We quantise the alpha channel half way up, so that alpha of
    # 0x80 or more is taken to be fully opaque and 0x7F or less is
    # fully transparent. Nasty, but the best we can do without
    # dithering (and don't even suggest we do that!).
    my $x;
    my $y;
    my $alpha = "";

    for ($y = 0; $y < $h; $y++) {
	my $currbyte = 0, $currbits = 0;
	for ($x = 0; $x < (($w+31)|31)-31; $x++) {
	    $pix = ($x < $w ? $data->[$y*$w+$x] : "\x00\x00\x00\xFF");
	    my @rgba = unpack "CCCC", $pix;
	    $currbyte <<= 1;
	    $currbits++;
	    if ($rgba[3] < 0x80) {
		if ($x < $w) {
		    $data->[$y*$w+$x] = undef;
		}
		$currbyte |= 1; # MS has the alpha channel inverted :-)
	    } else {
		# Might as well flip RGBA into BGR0 while we're here.
		if ($x < $w) {
		    $data->[$y*$w+$x] = pack "CCCC",
		      $rgba[2], $rgba[1], $rgba[0], 0;
		}
	    }
	    if ($currbits >= 8) {
		$alpha .= pack "C", $currbyte;
		$currbits -= 8;
	    }
	}
    }

    # For an 8-bit image, check we have at most 256 distinct
    # colours, and build the palette.
    %pal = ();
    if ($depth == 8) {
	my $palindex = 0;
	foreach $pix (@$data) {
	    next unless defined $pix;
	    $pal{$pix} = $palindex++ unless defined $pal{$pix};
	}
	die "too many colours in 8-bit image $filename\n" unless $palindex <= 256;
    } elsif ($depth == 4) {
	%pal = %win16pal;
    }

    my $raster = "";
    if ($depth < 24) {
	# For a non-24-bit image, flatten the image into one palette
	# index per pixel.
	my $currbyte = 0, $currbits = 0;
	for ($i = 0; $i < scalar @$data; $i++) {
	    $pix = $data->[$i];
	    $currbyte <<= $depth;
	    $currbits += $depth;
	    if (defined $pix) {
		if (!defined $pal{$pix}) {
		    die "illegal colour value $pix at pixel $i in $filename\n";
		}
		$currbyte |= $pal{$pix};
	    } else {
		$currbyte |= 0;
	    }
	    if ($currbits >= 8) {
		$raster .= pack "C", $currbyte;
		$currbits -= 8;
	    }
	}
    } else {
	# For a 24-bit image, reverse the order of the R,G,B values
	# and stick a padding zero on the end.
	for ($i = 0; $i < scalar @$data; $i++) {
	    if (defined $data->[$i]) {
		$raster .= $data->[$i];
	    } else {
		$raster .= "\x00\x00\x00\x00";
	    }
	}
	$depth = 32; # and adjust this
    }

    # Prepare the icon data. First the header...
    my $data = pack "VVVvvVVVVVV",
      40, # size of bitmap info header
      $w, # icon width
      $h*2, # icon height (x2 to indicate the subsequent alpha channel)
      1, # 1 plane (common to all MS image formats)
      $depth, # bits per pixel
      0, # no compression
      length $raster, # image size
      0, 0, 0, 0; # resolution, colours used, colours important (ignored)
    # ... then the palette ...
    if ($depth <= 8) {
	my $ncols = (1 << $depth);
	my $palette = "\x00\x00\x00\x00" x $ncols;
	foreach $i (keys %pal) {
	    substr($palette, $pal{$i}*4, 4) = $i;
	}
	$data .= $palette;
    }
    # ... the raster data we already had ready ...
    $data .= $raster;
    # ... and the alpha channel we already had as well.
    $data .= $alpha;

    # Prepare the header which will represent this image in the
    # icon file.
    my $header = pack "CCCCvvV",
      $w, $h, # width and height (this time the real height)
      1 << $depth, # number of colours, if less than 256
      0, # reserved
      1, # planes
      $depth, # bits per pixel
      length $data; # size of real icon data

    push @hdr, $header;
    push @dat, $data;
}
