#!/usr/bin/perl

use strict;
use warnings;

open my $footerfile, "<", shift @ARGV or die "footer: open: $!\n";
my $footer = "";
$footer .= $_ while <$footerfile>;
close $footerfile;

for my $arg (@ARGV) {
    $arg =~ /(.*\/)?([^\/]+)\.html$/ or die;
    my $filename = $2;
    open my $gamefile, "<", $arg or die "$arg: open: $!\n";
    my $unfinished = 0;
    my $docname = $filename;
    chomp(my $puzzlename = <$gamefile>);
    while ($puzzlename =~ s/^([^:=]+)(=([^:]+))?://) {
        if ($1 eq "unfinished") {
            $unfinished = 1;
        } elsif ($1 eq "docname") {
            $docname = $3;
        } else {
            die "$arg: unknown keyword '$1'\n";
        }
    }
    my $instructions = "";
    $instructions .= $_ while <$gamefile>;
    close $gamefile;

    open my $outpage, ">", "${filename}.html";

    my $unfinishedtitlefragment = $unfinished ? "an unfinished puzzle " : "";
    my $unfinishedheading = $unfinished ? "<h2 align=center>an unfinished puzzle</h2>\n" : "";
    my $unfinishedpara;
    my $links;
    if ($unfinished) {
        $unfinishedpara = <<EOF;
<p>
You have found your way to a page containing an <em>unfinished</em>
puzzle in my collection, not linked from the <a href="../">main
puzzles page</a>. Don't be surprised if things are hard to understand
or don't work as you expect.
EOF
        $links = <<EOF;
<p align="center">
<a href="../">Back to main puzzles page</a> (which does not link to this)
EOF
    } else {
        $unfinishedpara = "";
        $links = <<EOF;
<p align="center">
<a href="../doc/${docname}.html#${docname}">Full instructions</a>
|
<a href="../">Back to main puzzles page</a>
EOF
    }

    print $outpage <<EOF;
<html>
<head>
<title>${puzzlename}, ${unfinishedtitlefragment}from Simon Tatham's Portable Puzzle Collection</title>
<link rel="stylesheet" type="text/css" href="../../sitestyle.css" name="Simon Tatham's Home Page Style">
<script type="text/javascript" src="resize-puzzle-applet.js"></script>
</head>
<body onLoad="initResizablePuzzleApplet();">
<h1 align=center>${puzzlename}</h1>
${unfinishedheading}
<h2 align=center>from Simon Tatham's Portable Puzzle Collection</h2>

${unfinishedpara}

<p align="center">
<table cellpadding="0">
<tr>
<td>
<applet id="applet" archive="${filename}.jar" code="PuzzleApplet"
        width="700" height="500">
</applet>
</td>
<td>
<div id="eresize" style="width:5px;height:500px;cursor:e-resize;"></div>
</td>
</tr>
<td>
<div id="sresize" style="width:700px;height:5px;cursor:s-resize;"></div>
</td>
<td>
<div id="seresize" style="width:5px;height:5px;cursor:se-resize;"></div>
</td>
</tr>
</table>

${instructions}

${links}

${footer}
</body>
</html>
EOF

    close $outpage;
}
