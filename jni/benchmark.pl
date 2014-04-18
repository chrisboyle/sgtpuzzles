#!/usr/bin/perl

use strict;
use warnings;

my @presets = ();
my %presets = ();
my $maxval = 0;

while (<>) {
    chomp;
    if (/^(.*)(#.*): ([\d\.]+)$/) {
        push @presets, $1 unless defined $presets{$1};
        push @{$presets{$1}}, $3;
        $maxval = $3 if $maxval < $3;
    }
}

print <<EOF;
<!DOCTYPE html>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=ASCII" />
<title>Puzzle generation-time benchmarks</title>
<script type="text/javascript">
//<![CDATA[
function choose_scale_ticks(scale) {
    var nscale = 1, j = 0, factors = [2,2.5,2];
    while (scale / nscale > 20) {
        nscale *= factors[j];
        j = (j+1) % factors.length;
    } 
    return nscale;
}
function initPlots() {
    var canvases = document.getElementsByTagName('canvas');
    for (var i = 0; i < canvases.length; i++) {
	var canvas = canvases[i];
        var scale = eval(canvas.getAttribute("data-scale"));
        var add = 20.5, mult = (canvas.width - 2*add) / scale;
	var data = eval(canvas.getAttribute("data-points"));
        var ctx = canvas.getContext('2d');
        ctx.lineWidth = '1px';
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        ctx.strokeStyle = ctx.fillStyle = '#000000';
	if (data === "scale") {
            // Draw scale.
            ctx.font = "16px sans-serif";
            ctx.textAlign = "center";
            ctx.textBaseline = "alphabetic";
            var nscale = choose_scale_ticks(scale);
            for (var x = 0; x <= scale; x += nscale) {
                ctx.beginPath();
                ctx.moveTo(add+mult*x, canvas.height);
                ctx.lineTo(add+mult*x, canvas.height - 3);
                ctx.stroke();
                ctx.fillText(x + "s", add+mult*x, canvas.height - 6);
            }
        } else {
            // Draw a box plot.
            function quantile(x) {
                var n = (data.length * x) | 0;
                return (data[n-1] + data[n]) / 2;
            }

            var q1 = quantile(0.25), q2 = quantile(0.5), q3 = quantile(0.75);
            var iqr = q3 - q1;
            var top = 0.5, bot = canvas.height - 1.5, mid = (top+bot)/2;
            var wlo = null, whi = null; // whisker ends

            ctx.strokeStyle = '#bbbbbb';
            var nscale = choose_scale_ticks(scale);
            for (var x = 0; x <= scale; x += nscale) {
                ctx.beginPath();
                ctx.moveTo(add+mult*x, 0);
                ctx.lineTo(add+mult*x, canvas.height);
                ctx.stroke();
            }
            ctx.strokeStyle = '#000000';

            for (var j in data) {
                var x = data[j];
                if (x >= q1 - 1.5 * iqr && x <= q3 + 1.5 * iqr) {
                    if (wlo === null || wlo > x)
                        wlo = x;
                    if (whi === null || whi < x)
                        whi = x;
                } else {
                    ctx.beginPath();
                    ctx.arc(add+mult*x, mid, 2, 0, 2*Math.PI);
                    ctx.stroke();
                    if (x >= q1 - 3 * iqr && x <= q3 + 3 * iqr)
                        ctx.fill();
                }
            }

            ctx.beginPath();

            // Box
            ctx.moveTo(add+mult*q1, top);
            ctx.lineTo(add+mult*q3, top);
            ctx.lineTo(add+mult*q3, bot);
            ctx.lineTo(add+mult*q1, bot);
            ctx.closePath();

            // Line at median
            ctx.moveTo(add+mult*q2, top);
            ctx.lineTo(add+mult*q2, bot);

            // Lower whisker
            ctx.moveTo(add+mult*q1, mid);
            ctx.lineTo(add+mult*wlo, mid);
            ctx.moveTo(add+mult*wlo, top);
            ctx.lineTo(add+mult*wlo, bot);

            // Upper whisker
            ctx.moveTo(add+mult*q3, mid);
            ctx.lineTo(add+mult*whi, mid);
            ctx.moveTo(add+mult*whi, top);
            ctx.lineTo(add+mult*whi, bot);

            ctx.stroke();
        }
    }
}
//]]>
</script>
</head>
<body onLoad="initPlots();">
<h1 align=center>Puzzle generation-time benchmarks</h1>
<table>
<tr><th>Preset</th><td><canvas width=700 height=30 data-points='"scale"' data-scale="$maxval"></td></tr>
EOF

for my $preset (@presets) {
    print "<tr><td>", &escape($preset), "</td><td><canvas width=700 height=15 data-points=\"[";
    print join ",", sort { $a <=> $b } @{$presets{$preset}};
    print "]\" data-scale=\"$maxval\"></td></tr>\n";
}

print <<EOF;
</body>
</html>
EOF

sub escape {
    my ($text) = @_;
    $text =~ s/&/&amp;/g;
    $text =~ s/</&lt;/g;
    $text =~ s/>/&gt;/g;
    return $text;
}
