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
<meta http-equiv="Content-Type" content="text/html; charset=ASCII" />
<title>${puzzlename}, ${unfinishedtitlefragment}from Simon Tatham's Portable Puzzle Collection</title>
<script type="text/javascript" src="${filename}.js"></script>
</head>
<body onLoad="initPuzzle();">
<h1 align=center>${puzzlename}</h1>
${unfinishedheading}
<h2 align=center>from Simon Tatham's Portable Puzzle Collection</h2>

${unfinishedpara}

<hr>
<p align=center>
  <input type="button" id="new" value="New game">
  <input type="button" id="restart" value="Restart game">
  <input type="button" id="undo" value="Undo move">
  <input type="button" id="redo" value="Redo move">
  <input type="button" id="solve" value="Solve game">
  <input type="button" id="specific" value="Enter game ID">
  <input type="button" id="random" value="Enter random seed">
  <select id="gametype"></select>
</p>
<p align=center>
  <table cellpadding="0" cellspacing="0">
    <tr>
      <td>
        <canvas id="puzzlecanvas" width="1" height="1" tabindex="1">
      </td>
    <tr>
      <td id="statusbarholder">
      </td>
    </tr>
  </table>
</p>
<p align=center>
  Link to this puzzle:
  <a id="permalink-desc">by game ID</a>
  <a id="permalink-seed">by random seed</a>
</p>
<hr>

${instructions}

${links}

${footer}
</body>
</html>
EOF

    close $outpage;
}
