#!/usr/bin/perl

# Perl script to generate a .INF file for building a Pocket PC .CAB
# archive of Puzzles. This has to be scripted so that it can read
# wingames.lst and automatically adjust to the current available
# set of puzzles.

# Usage:
#
#   $ ./wceinf.pl wingames.lst > puzzles.inf

$lst = shift @ARGV;
open LST, "<", $lst;
while (<LST>) {
    chomp;
    split /:/;
    push @exes, $_[0];
    $names{$_[0]} = $_[1];
}
close LST;

print '[Version]'."\n";
print 'Signature   = "$Windows NT$"    ; required as-is'."\n";
print 'Provider    = "Simon Tatham"    ; full app name will be "<Provider> <AppName>"'."\n";
print 'CESignature = "$Windows CE$"    ; required as-is'."\n";
print ''."\n";
print '[CEStrings]'."\n";
print 'AppName     = "Puzzle Collection"    ; full app name will be "<Provider> <AppName>"'."\n";
print 'InstallDir  = %CE8%\%AppName%        ; "\Program Files\Games\Puzzle Collection" (default install directory)'."\n";
print ''."\n";
print '[CEDevice.x86]'."\n";
print 'ProcessorType = 686'."\n";
print ''."\n";
print '[CEDevice.ARM]'."\n";
print 'ProcessorType = 2577'."\n";
print ''."\n";
print '[SourceDisksNames.x86]             ; CPU-dependent files'."\n";
print '2 = ,"x86 Files",,.'."\n";
print ''."\n";
print '[SourceDisksNames.ARMV4]           ; CPU-dependent files'."\n";
print '2 = ,"ARM Files",,.'."\n";
print ''."\n";
print '[SourceDisksFiles]'."\n";
for $exe (@exes) {
    print $exe.' = 2'."\n";
}
print ''."\n";
print '[DefaultInstall]'."\n";
print 'CopyFiles   = PuzzleFiles'."\n";
print 'CEShortcuts = Links'."\n";
print ''."\n";
print '[DestinationDirs]'."\n";
print 'PuzzleFiles = 0, %InstallDir%'."\n";
print 'Links       = 0, %CE14%\Puzzles'."\n";
print ''."\n";
print ';File copy list.'."\n";
print '[PuzzleFiles]'."\n";
for $exe (@exes) {
    print $exe."\n";
}
print ''."\n";
print '[Links]'."\n";
for $exe (@exes) {
    print '"'.$names{$exe}.'",0,'.$exe."\n";
}
