#!/usr/bin/perl

# Make .desktop files for the puzzles.
#
# At present, this script is intended for developer usage: if you're
# working on the puzzles and want to play your bleeding-edge locally
# modified and compiled versions, run this script and it will create a
# collection of desktop files in ~/.local/share/applications where
# XFCE can pick them up and add them to its main menu. (Be sure to run
# 'xfdesktop --reload' after running this.)
#
# (If you don't use XFCE, patches to support other desktop
# environments are welcome :-)

use strict;
use warnings;
use Cwd 'abs_path';

die "usage: desktop.pl [<outdir> [<bindir> <icondir>]]\n"
    unless @ARGV == 0 or @ARGV == 1 or @ARGV == 3;

my ($outdir, $bindir, $icondir) = @ARGV;
$outdir = $ENV{'HOME'}."/.local/share/applications" unless defined $outdir;
$bindir = "." unless defined $bindir;
$icondir = "./icons" unless defined $icondir;
$bindir = abs_path($bindir);
$icondir = abs_path($icondir);

open my $desc, "<", "gamedesc.txt"
    or die "gamedesc.txt: open: $!\n";

while (<$desc>) {
    chomp;
    my ($id, $win, $displayname, $description, $summary) = split /:/, $_;

    open my $desktop, ">", "$outdir/$id.desktop"
        or die "$outdir/$id.desktop: open: $!\n";

    my $path = "$bindir/$id";
    my $upath = "$bindir/unfinished/$id";
    $path = $upath if ! -f $path && -f $upath;

    print $desktop "[Desktop Entry]\n";
    print $desktop "Version=1.0\n";
    print $desktop "Type=Application\n";
    print $desktop "Name=$displayname\n";
    print $desktop "Comment=$description\n";
    print $desktop "Exec=$path\n";
    print $desktop "Icon=$icondir/$id-48d24.png\n";
    print $desktop "StartupNotify=false\n";
    print $desktop "Categories=Game;\n";
    print $desktop "Terminal=false\n";

    close $desktop
        or die "$outdir/$id.desktop: close: $!\n";
}
