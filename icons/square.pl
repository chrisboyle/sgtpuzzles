#!/usr/bin/perl 

# Read an input image, crop its border to a standard width, and
# convert it into a square output image. Parameters are:
#
#  - the required total image size
#  - the output border thickness
#  - the input image file name
#  - the output image file name.

($convert, $osize, $oborder, $infile, $outfile) = @ARGV;

# Determine the input image's size.
$ident = `identify -format "%w %h" $infile`;
$ident =~ /(\d+) (\d+)/ or die "unable to get size for $infile\n";
($w, $h) = ($1, $2);

# Read the input image data.
$data = [];
open IDATA, "-|", $convert, "-depth", "8", $infile, "rgb:-";
push @$data, $rgb while (read IDATA,$rgb,3,0) == 3;
close IDATA;
# Check we have the right amount of data.
$xl = $w * $h;
$al = scalar @$data;
die "wrong amount of image data ($al, expected $xl) from $infile\n"
  unless $al == $xl;

# Find the background colour, by looking around the entire border
# and finding the most popular pixel colour.
for ($i = 0; $i < $w; $i++) {
    $pcount{$data->[$i]}++; # top row
    $pcount{$data->[($h-1)*$w+$i]}++; # bottom row
}
for ($i = 1; $i < $h-1; $i++) {
    $pcount{$data->[$i*$w]}++; # left column
    $pcount{$data->[$i*$w+$w-1]}++; # right column
}
@plist = sort { $pcount{$b} <=> $pcount{$a} } keys %pcount;
$back = $plist[0];

# Crop rows and columns off the image to find the central rectangle
# of non-background stuff.
$ystart = 0;
$ystart++ while $ystart < $h - 1 and scalar(grep { $_ ne $back } map { $data->[$ystart*$w+$_] } 0 .. ($w-1)) == 0;
$yend = $h-1;
$yend-- while $yend > $ystart and scalar(grep { $_ ne $back } map { $data->[$yend*$w+$_] } 0 .. ($w-1)) == 0;
$xstart = 0;
$xstart++ while $xstart < $w - 1 and scalar(grep { $_ ne $back } map { $data->[$_*$w+$xstart] } 0 .. ($h-1)) == 0;
$xend = $w-1;
$xend-- while $xend > $xstart and scalar(grep { $_ ne $back } map { $data->[$_*$w+$xend] } 0 .. ($h-1)) == 0;

# Decide how much border we're going to put back on to make the
# image perfectly square.
$hexpand = ($yend-$ystart) - ($xend-$xstart);
if ($hexpand > 0) {
    $left = int($hexpand / 2);
    $xstart -= $left;
    $xend += $hexpand - $left;
} elsif ($hexpand < 0) {
    $vexpand = -$hexpand;
    $top = int($vexpand / 2);
    $ystart -= $top;
    $yend += $vexpand - $top;
}
$ow = $xend - $xstart + 1;
$oh = $yend - $ystart + 1;
die "internal computation problem" if $ow != $oh; # should be square

# And decide how much _more_ border goes on to add the bit around
# the edge.
$realow = int($ow * ($osize / ($osize - 2*$oborder)));
$extra = $realow - $ow;
$left = int($extra / 2);
$xstart -= $left;
$xend += $extra - $left;
$top = int($extra / 2);
$ystart -= $top;
$yend += $extra - $top;
$ow = $xend - $xstart + 1;
$oh = $yend - $ystart + 1;
die "internal computation problem" if $ow != $oh; # should be square

# Now write out the resulting image, and resize it appropriately.
open IDATA, "|-", $convert, "-size", "${ow}x${oh}", "-depth", "8", "-resize", "${osize}x${osize}!", "rgb:-", $outfile;
for ($y = $ystart; $y <= $yend; $y++) {
    for ($x = $xstart; $x <= $xend; $x++) {
	if ($x >= 0 && $x < $w && $y >= 0 && $y < $h) {
	    print IDATA $data->[$y*$w+$x];
	} else {
	    print IDATA $back;
	}
    }
}
close IDATA;
