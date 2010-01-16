#!/usr/bin/perl

use strict;

my @sources = <*.c android/AndroidManifest.xml android/res/menu/*.xml android/res/layout/*.xml android/src/name/boyle/chris/sgtpuzzles/*.java>;
my @stringsfiles = <android/res/values*/strings.xml>;
my %srcstrings;
my %resstrings;

sub mangle($)
{
	my ($_) = ( @_ );
	s/\%age/percentage/g;
	s/','/comma/g;
	s/\%[0-9.]*[sd]/X/g;
	s/[^A-Za-z0-9]+/_/g;
	s/^([0-9])/_$1/;
	s/_$//;
	return $_;
}

my $problem = 0;

for my $lang ( @stringsfiles ) {
	open(RES,$lang) or die "Can't open $lang: $!\n";
	while (<RES>) {
		chomp;
		next unless /<string\s+name="([^"]+)">(.*)<\/string>/;
		$resstrings{$lang}->{$1} = [ $2, "$lang:$." ];
	}
	close(RES);
}

for my $source (@sources) {
	my $isgame = 0;
	open(SRC,$source) or die "Can't open $source: $!\n";
	while (<SRC>) {
		chomp;
		$isgame = 1 if /^#define\s+thegame\s+/ && $source ne 'nullgame.c';
		s/\\"/''/g;
		while (/_\(\s*"(.*?)"\s*\)/g) {
			my $quoted = $1;
			for my $str ( $quoted =~ /^:/ ? split(/:/,substr($quoted,1)) : ( $quoted ) ) {
				my $id = mangle($str);
#				print "set \"$id\"\n";
				$srcstrings{$id} = [ $str, "$source:$." ];
			}
		}
		while (/(?:[^.]R\.string\.|\@string\/)([A-Za-z0-9_]+)/g) {
			$srcstrings{$1} = [ undef, "$source:$." ];
		}
	}
	close(SRC);
	if ($isgame) {
		my $name = $source;
		$name =~ s/\.c$//;
		$srcstrings{"name_$name"} = [ undef, "(filename)" ];
		$srcstrings{"desc_$name"} = [ undef, "(filename)" ];
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
		if ( ! defined $srcstrings{$id} ) {
			warn "String resource $lang:$id might be unused, from $resstrings{$lang}->{$id}[1]\n";
			$problem = 1;
		}
	}
}

exit $problem;
