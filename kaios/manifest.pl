#!/usr/bin/perl

use strict;
use warnings;

use JSON::PP;

@ARGV == 4 or
    die "usage: manifest.pl <name> <displayname> <description> <objective>";
my ($name, $displayname, $description, $objective) = @ARGV;

# This can be overridden by the build script.
my $version = "Unidentified build";

# Limits from
# https://developer.kaiostech.com/docs/getting-started/main-concepts/manifest
length($displayname) <= 20 or die "Name too long: $displayname";
length($description) <= 40 or die "Subtitle too long: $description";
$objective .= "  Part of Simon Tatham's Portable Puzzle Collection.";
# https://developer.kaiostech.com/docs/distribution/submission-guideline
length($objective) <= 220 or die "Description too long: $objective";

my $decvers;
if ($version =~ /^20(\d\d)(\d\d)(\d\d)\./) {
    # Fabricate a dotted-decimal version number as required by KaiOS.
    # The precise requirements are unclear and violating them leads to
    # messes in the KaiStore that can only be resolved by Developer
    # Support.  Specifically, uploading a bad version number as the
    # first upload of an app can make it impossible to upload a new
    # version.  I hope that three components of two digits each will
    # be acceptable.
    $decvers = join('.', $1+0, $2+0, $3+0);
}

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
    locales => {
        "en-GB" => {
            name => $displayname,
            subtitle => $description,
            description => $objective,
        },
    },
    categories => ["games"],
    type => "web",
    cursor => JSON::PP::false,
    # These permissions could be removed on builds without KaiAds,
    # but that's a bit complicated.
    permissions => {
        mobiledata => {
            description => "Required to display advertisements"
        },
        wifidata => {
            description => "Required to display advertisements"
        },
    },
    $decvers ? (version => $decvers) : (),
})
