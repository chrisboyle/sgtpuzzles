#!/usr/bin/perl

# Process the raw output from benchmark.sh into Javascript-ified HTML.

use strict;
use warnings;

my @presets = ();
my %presets = ();
my $maxval = 0;

while (<<>>) {
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
    document.getElementById('sort_orig').onclick = function() {
        sort(function(e) {
            return parseFloat(e.getAttribute("data-index"));
        });
    };
    document.getElementById('sort_median').onclick = function() {
        sort(function(e) {
            return -parseFloat(e.getAttribute("data-median"));
        });
    };
    document.getElementById('sort_mean').onclick = function() {
        sort(function(e) {
            return -parseFloat(e.getAttribute("data-mean"));
        });
    };
}
function sort(keyfn) {
    var rows = document.getElementsByTagName("tr");
    var trs = [];
    for (var i = 0; i < rows.length; i++)
        trs.push(rows[i]);
    trs.sort(function(a,b) {
        var akey = keyfn(a);
        var bkey = keyfn(b);
        return akey < bkey ? -1 : akey > bkey ? +1 : 0;
    });
    var parent = trs[0].parentElement;
    for (var i = 0; i < trs.length; i++)
        parent.removeChild(trs[i]);
    for (var i = 0; i < trs.length; i++)
        parent.appendChild(trs[i]);
}
//]]>
</script>
</head>
<body onLoad="initPlots();">
<h1 align=center>Puzzle generation-time benchmarks</h1>
<p>Sort order:
<button id="sort_orig">Original</button>
<button id="sort_median">Median</button>
<button id="sort_mean">Mean</button>
<table>
<tr><th>Preset</th><td><canvas width=700 height=30 data-points='"scale"' data-scale="$maxval"></td></tr>
EOF

my $index = 0;
for my $preset (@presets) {
    my @data = sort { $a <=> $b } @{$presets{$preset}};
    my $median = ($#data % 2 ?
                  ($data[($#data-1)/2]+$data[($#data+1)/2])/2 :
                  $data[$#data/2]);
    my $mean = 0; map { $mean += $_ } @data; $mean /= @data;
    print "<tr data-index=\"$index\" data-mean=\"$mean\" data-median=\"$median\"><td>", &escape($preset), "</td><td><canvas width=700 height=15 data-points=\"[";
    print join ",", @data;
    print "]\" data-scale=\"$maxval\"></td></tr>\n";
    $index++;
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
