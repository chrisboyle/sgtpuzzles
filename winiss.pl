#!/usr/bin/perl

# Perl script to generate an Inno Setup installer script for
# Puzzles. This has to be scripted so that it can read gamedesc.txt
# and automatically adjust to the current available set of puzzles.

# Usage:
#
#   $ ./winiss.pl 20140922.sdfsdf gamedesc.txt > puzzles.iss
#
# where the first argument is the version number which will be encoded
# in the installer's version indicators. The first component of that
# version number will be expected to be a YYYYMMDD-format date.

use warnings;
use Time::Local;

$ver = shift @ARGV;

# Parse the date out of $ver, and convert it into an integer number of
# days since an arbitrary epoch. This number is used for the Windows
# version resource (which wants a monotonic 16-bit integer). The epoch
# is chosen so that the first build using this date-based mechanism
# has a higher number than the last build in which that number was
# derived from a Subversion revision.
die "bad date format" if $ver !~ /^(\d{4})(\d{2})(\d{2})/;
$date = timegm(0,0,0,$3,$2-1,$1);
$integer_date = int($date / 86400) - 6000;

$desc = shift @ARGV;
open DESC, "<", $desc;
while (<DESC>) {
    chomp;
    @_ = split /:/;
    push @exes, $_[1];
    $names{$_[1]} = $_[2];
}
close DESC;

print '; -*- no -*-'."\n";
print ';'."\n";
print '; -- Inno Setup installer script for Puzzles.'."\n";
print ''."\n";
print '[Setup]'."\n";
print 'AppName=Simon Tatham\'s Portable Puzzle Collection'."\n";
print 'AppVerName=Puzzles version '.$ver."\n";
print 'VersionInfoTextVersion=Version '.$ver."\n";
print 'AppVersion=r'.$ver."\n";
print 'VersionInfoVersion=0.0.'.$integer_date.'.0'."\n";
print 'AppPublisher=Simon Tatham'."\n";
print 'AppPublisherURL=http://www.chiark.greenend.org.uk/~sgtatham/puzzles/'."\n";
print 'DefaultDirName={pf}\Simon Tatham\'s Portable Puzzle Collection'."\n";
print 'DefaultGroupName=Simon Tatham\'s Puzzles'."\n";
# print 'SetupIconFile=fixmethinkoneup.ico'."\n";
# print 'UninstallDisplayIcon={app}\fixmethinkoneup.exe'."\n";
print 'ChangesAssociations=no'."\n";
print 'Compression=zip/9'."\n";
print 'AllowNoIcons=yes'."\n";
print ''."\n";
print '[Files]'."\n";
for $exe (@exes) {
    print 'Source: "'.$exe.'"; DestDir: "{app}"; Flags: promptifolder replacesameversion uninsrestartdelete'."\n";
}
print 'Source: "website.url"; DestDir: "{app}"; Flags: uninsrestartdelete'."\n";
print 'Source: "puzzles.chm"; DestDir: "{app}"; Flags: uninsrestartdelete'."\n";
print 'Source: "puzzles.hlp"; DestDir: "{app}"; Flags: uninsrestartdelete'."\n";
print 'Source: "puzzles.cnt"; DestDir: "{app}"; Flags: uninsrestartdelete'."\n";
print 'Source: "LICENCE"; DestDir: "{app}"; Flags: uninsrestartdelete'."\n";
print ''."\n";
print '[Icons]'."\n";
for $exe (@exes) {
    print 'Name: "{group}\\'.$names{$exe}.'"; Filename: "{app}\\'.$exe.'"'."\n";
}
print '; We have to fall back from the .chm to the older .hlp file on some Windows'."\n";
print '; versions.'."\n";
print 'Name: "{group}\Puzzles Manual"; Filename: "{app}\puzzles.chm"; MinVersion: 4.1,5.0'."\n";
print 'Name: "{group}\Puzzles Manual"; Filename: "{app}\puzzles.hlp"; OnlyBelowVersion: 4.1,5.0'."\n";
print 'Name: "{group}\Puzzles Web Site"; Filename: "{app}\website.url"'."\n";
