#!/usr/bin/perl

use strict;
use warnings;

use JSON::PP;

@ARGV == 4 or
    die "usage: manifest.pl <name> <displayname> <description> <objective>";
my ($name, $displayname, $description, $objective) = @ARGV;

# Limits from
# https://developer.kaiostech.com/docs/getting-started/main-concepts/manifest
length($displayname) <= 20 or die "Name too long: $displayname";
length($description) <= 40 or die "Subtitle too long: $description";
$objective .= "  Part of Simon Tatham's Portable Puzzle Collection.";
# https://developer.kaiostech.com/docs/distribution/submission-guideline
length($objective) <= 220 or die "Description too long: $objective";

print encode_json({
    name => $displayname,
    subtitle => $description,
    description => $objective,
    launch_path => "/${name}.html",
    icons => {
        "56" =>  "/${name}-56kai.png",
        "112" => "/${name}-112kai.png",
    },
    developer => {
        name => "Ben Harris",
        url => "https://bjh21.me.uk",
    },
    default_locale => "en-GB",
    categories => ["games"],
    cursor => JSON::PP::false,
})
