#!/usr/bin/perl

# Perl script to generate an Inno Setup installer script for
# Puzzles. This has to be scripted so that it can read gamedesc.txt
# and automatically adjust to the current available set of puzzles.

# Usage:
#
#   $ ./winiss.pl 1234 gamedesc.txt > puzzles.iss
#
# where `1234' is the revision number which will be encoded in the
# installer's version indicators.

use warnings;

$rev = shift @ARGV;
($revclean=$rev) =~ s/M$//;
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
print 'AppVerName=Puzzles revision '.$rev."\n";
print 'VersionInfoTextVersion=Revision '.$rev."\n";
print 'AppVersion=r'.$rev."\n";
print 'VersionInfoVersion=0.0.'.$revclean.'.0'."\n";
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
