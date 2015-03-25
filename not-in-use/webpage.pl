#!/usr/bin/perl

# Construct the two pieces of my main puzzle collection web page that
# need to vary with the set of puzzles: the big list of <span>s with
# puzzle pictures and links etc, and the list of Windows executable
# files down in the downloads section.

use strict;
use warnings;
use HTML::Entities;

open my $desc, "<", "gamedesc.txt"
    or die "gamedesc.txt: open: $!\n";

open my $spans, ">", "wwwspans.html"
    or die "wwwspans.html: open: $!\n";

open my $links, ">", "wwwlinks.html"
    or die "wwwspans.html: open: $!\n";

my $n = 0;
while (<$desc>) {
    chomp;
    my ($id, $win, $displayname, $description, $summary) = split /:/, $_;

    printf $spans
        '<span class="puzzle"><table>'.
        '<tr><th align="center">%s</th></tr>'.
        '<tr><td align="center">'.
        '<img style="margin: 0.5em" alt="" title="%s" width=150 height=150 border=0 src="%s-web.png" />'.
        '</td></tr>'.
        '<tr><td align="center" style="font-size: 70%%"><code>[</code>'.
        ' <a href="java/%s.html">java</a> '.
        '|'.
        ' <a href="js/%s.html">js</a> '.
        '|'.
        ' <a href="doc/%s.html#%s">manual</a> '.
        '<code>]</code><br><code>[</code>'.
        ' <a href="%s"><code>%s</code></a> '.
        '<code>]</code></td></tr>'.
        '<tr><td align="center">%s</td></tr></table></span>'.
        "\n",
        encode_entities($displayname),
        encode_entities($description),
        encode_entities($id),
        encode_entities($id),
        encode_entities($id),
        encode_entities($id),
        encode_entities($id),
        encode_entities($win),
        encode_entities($win),
        encode_entities($summary);

    if ($n > 0) {
        if ($n % 5 == 0) {
            print $links "<br />";
        } else {
            print $links " | ";
        }
    }
    printf $links '<a href="%s">%s</a>',
    encode_entities($win), encode_entities($win);

    $n++;
}

close $desc;
close $spans;
close $links;
