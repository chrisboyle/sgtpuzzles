#!/usr/bin/perl

use strict;

my @sources = <jni/*.c AndroidManifest.xml res/menu/*.xml res/layout/*.xml res/xml/preferences.xml res/values/arrays.xml java/name/boyle/chris/sgtpuzzles/*.java>;
my @stringsfiles = <res/values*/strings.xml>;
my %srcstrings;
my %resstrings;
my %optionalStrings;

sub mangle($)
{
	my ($id) = ( @_ );
	$id =~ s/\%age/percentage/g;
	$id =~ s/','/comma/g;
	$id =~ s/\\n/_/g;
	$id =~ s/\%[0-9.]*[sd]/X/g;
	$id =~ s/[^A-Za-z0-9]+/_/g;
	$id =~ s/^([0-9])/_$1/;
	$id =~ s/_$//;
	return $id;
}

my $problem = 0;

for my $lang ( @stringsfiles ) {
	open(RES,$lang) or die "Can't open $lang: $!\n";
	$_ = join('',<RES>);
	close(RES);
	while (/<string\s+name="([^"]+)"(?: formatted="false")?>(.*?)<\/string>/gs) {
		$resstrings{$lang}->{$1} = [ $2, "$lang:$." ];
	}
}

for my $source (@sources) {
	my $isgame = 0;
	open(SRC,$source) or die "Can't open $source: $!\n";
	$_ = join('',<SRC>);
	close(SRC);
	s/\\"/''/g;
	$isgame = 1 if /^#define\s+thegame\s+/m && $source ne 'jni/nullgame.c';
	while (/_\(\s*"(.*?)"\s*\)/gs) {
		my $quoted = $1;
		for my $str ( $quoted =~ /^:/ ? split(/:/,substr($quoted,1)) : ( $quoted ) ) {
			my $id = mangle($str);
#			print "set \"$id\"\n";
			$srcstrings{$id} = [ $str, "$source:$." ];
		}
	}
	while (/(?:[^.]R\.string\.|\@string\/)([A-Za-z0-9_]+)/g) {
		$srcstrings{$1} = [ undef, "$source:$." ] unless $1 =~ /api_key/;
	}
	if ($isgame) {
		my $name = $source;
		$name =~ s/jni\/(.*)\.c$/$1/;
		$srcstrings{"name_$name"} = [ undef, "(filename)" ];
		$srcstrings{"desc_$name"} = [ undef, "(filename)" ];
		$optionalStrings{"toast_no_arrows_$name"} = [ undef, "(filename)" ];
	}

}

for my $id ( keys %srcstrings ) {
	for my $lang ( keys %resstrings ) {
		if ( ! defined $resstrings{$lang}->{$id} ) {
			warn "No string resource in $lang for $id from $srcstrings{$id}[1]\n";
			$problem = 1 unless $id =~ /^desc_/;
		}
	}
}

for my $lang ( keys %resstrings ) {
	for my $id ( keys %{$resstrings{$lang}} ) {
		if ( ! defined $srcstrings{$id} && ! defined $optionalStrings{$id} ) {
			warn "String resource $lang:$id might be unused, from $resstrings{$lang}->{$id}[1]\n";
			$problem = 1;
		}
	}
}

# TODO check optional strings are the same key set in each language

print( (scalar keys %srcstrings) . " source strings, " . (scalar keys %resstrings) . " languages: " . join(', ', map { $_."=".(scalar keys %{$resstrings{$_}}) } keys %resstrings )."\n");

exit $problem;
