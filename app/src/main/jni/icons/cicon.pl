#!/usr/bin/perl 

# Given a list of input PNGs, create a C source file file
# containing a const array of XPMs, under the name `xpm_icon'.

$k = 0;
@xpms = ();
$convert = shift @ARGV;
foreach $f (@ARGV) {
  # XPM format is generated directly by ImageMagick, so that's easy
  # enough. We just have to adjust the declaration line so that it
  # has the right name, linkage and storage class.
  @lines = ();
  open XPM, "-|", $convert, $f, "xpm:-";
  push @lines, $_ while <XPM>;
  close XPM;
  die "XPM from $f in unexpected format\n" unless $lines[1] =~ /^static.*\{$/;
  $lines[1] = "static const char *const xpm_icon_$k"."[] = {\n";
  $k++;
  push @xpms, @lines, "\n";
}

# Now output.
print "#include \"gtk.h\"\n"; # for declarations of xpm_icons and n_xpm_icons
foreach $line (@xpms) { print $line; }
print "const char *const *const xpm_icons[] = {\n";
for ($i = 0; $i < $k; $i++) { print "    xpm_icon_$i,\n"; }
print "};\n";
print "const int n_xpm_icons = $k;\n";
