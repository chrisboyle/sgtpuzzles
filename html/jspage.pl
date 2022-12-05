#!/usr/bin/perl

use strict;
use warnings;

my $jspath = "";
while ($ARGV[0] =~ /^-/) {
    my $opt = shift @ARGV;
    last if $opt eq "--";
    if ($opt =~ /^--jspath=(.+)$/) {
        $jspath = $1;
    } else {
        die "jspage.pl: unrecognised option '$opt'\n";
    }
}

open my $footerfile, "<", shift @ARGV or die "footer: open: $!\n";
my $footer = "";
$footer .= $_ while <$footerfile>;
close $footerfile;

for my $arg (@ARGV) {
    $arg =~ /(.*\/)?([^\/]+)\.html$/ or die;
    my $filename = $2;
    open my $gamefile, "<", $arg or die "$arg: open: $!\n";
    my $unfinished = 0;
    my $docname = $filename;
    chomp(my $puzzlename = <$gamefile>);
    while ($puzzlename =~ s/^([^:=]+)(=([^:]+))?://) {
        if ($1 eq "unfinished") {
            $unfinished = 1;
        } elsif ($1 eq "docname") {
            $docname = $3;
        } else {
            die "$arg: unknown keyword '$1'\n";
        }
    }
    my $instructions = "";
    $instructions .= $_ while <$gamefile>;
    close $gamefile;

    open my $outpage, ">", "${filename}.html";

    my $unfinishedtitlefragment = $unfinished ? "an unfinished puzzle " : "";
    my $unfinishedheading = $unfinished ? "<h2 align=center>an unfinished puzzle</h2>\n" : "";
    my $unfinishedpara;
    my $links;
    if ($unfinished) {
        $unfinishedpara = <<EOF;
<p>
You have found your way to a page containing an <em>unfinished</em>
puzzle in my collection, not linked from the <a href="../">main
puzzles page</a>. Don't be surprised if things are hard to understand
or don't work as you expect.
EOF
        $links = <<EOF;
<p align="center">
<a href="../">Back to main puzzles page</a> (which does not link to this)
EOF
    } else {
        $unfinishedpara = "";
        $links = <<EOF;
<p align="center">
<a href="../doc/${docname}.html#${docname}">Full instructions</a>
|
<a href="../">Back to main puzzles page</a>
EOF
    }

    print $outpage <<EOF;
<!DOCTYPE html>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=ASCII" />
<title>${puzzlename}, ${unfinishedtitlefragment}from Simon Tatham's Portable Puzzle Collection</title>
<script defer type="text/javascript" src="${jspath}${filename}.js"></script>
<style>
/* Top-level form for the game menu */
#gamemenu {
    margin-top: 0;
    margin-bottom: 0.375em;
    /* Add a little mild text formatting */
    font-weight: bold;
    font-size: 0.8em;
    text-align: center
}

main {
    text-align: center;
}

/* Inside that form, the main menu bar and every submenu inside it is a <ul> */
#gamemenu ul {
    list-style: none;  /* get rid of the normal unordered-list bullets */
    /* make top-level menu bar items appear side by side */
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    margin: 0;
    /* Compensate for the negative margins on menu items by adding a
     * little bit of padding so that the borders of the items don't protrude
     * beyond the menu. */
    padding: 0.5px;
}

/* Individual menu items are <li> elements within such a <ul> */
#gamemenu li {
    /* Suppress the text-selection I-beam pointer */
    cursor: default;
    /* Surround each menu item with a border. */
    border: 1px solid rgb(180,180,180);
    /* Arrange that the borders of each item overlap the ones next to it. */
    margin: -0.5px;
    /* Set position:relative, so that if this item has a submenu it can
     * position itself relative to the parent item. */
    position: relative;
}

#gamemenu li[role=separator] {
    width: 1.5em;
    border: 0;
}

/* The interactive contents of menu items are their child elements. */
#gamemenu li > * {
    /* Line height and padding appropriate to top-level menu items */
    padding: 0.2em 0.75em;
    margin: 0;
    display: block;
}

#gamemenu :disabled {
    /* Grey out disabled buttons */
    color: rgba(0,0,0,0.5);
}

#gamemenu li > :hover:not(:disabled),
#gamemenu li > .focus-within {
    /* When the mouse is over a menu item, highlight it */
    background: rgba(0,0,0,0.3);
}

\@media (max-width:18em) {
    /* Suppress some words in top-level menu items when viewport
     * is very small */
    .verbiage {
        display: none;
    }
}

#gamemenu ul ul {
    /* Second-level menus and below are not displayed by default */
    display: none;
    /* When they are displayed, they are positioned immediately below
     * their parent <li>, and with the left edge aligning */
    position: absolute;
    top: 100%;
    left: 0;
    /* Switch to vertical stacking for drop-down submenus */
    flex-direction: column;
    /* We must specify an explicit background colour for submenus, because
     * they must be opaque (don't want other page contents showing through
     * them). */
    background: white;
    /* And make sure they appear in front. */
    z-index: 50;
}

#gamemenu ul ul.left {
    /* A second-level menu with class "left" aligns its right edge with
     * its parent, rather than its left edge */
    left: inherit; right: 0;
}

/* Menu items in second-level menus and below */
#gamemenu li li {
    /* Inhibit wrapping, so the submenu will expand its width as needed. */
    white-space: nowrap;
    /* Override the text-align:center from above */
    text-align: left;
}

#gamemenu ul ul ul {
    /* Third-level submenus are drawn to the side of their parent menu
     * item, not below it */
    top: 0; left: 100%;
}

#gamemenu ul ul ul.left {
    /* A submenu with class "left" goes to the left of its parent,
     * not the right */
    left: inherit; right: 100%;
}

#gamemenu :hover > ul,
#gamemenu .focus-within > ul {
    /* Last but by no means least, the all-important line that makes
     * submenus be displayed! Any <ul> whose parent <li> is being
     * hovered over gets display:flex overriding the display:none
     * from above. */
    display: flex;
}

#gamemenu button {
    /* Menu items that trigger an action.  We put some effort into
     * removing the default button styling. */
    -moz-appearance: none;
    -webkit-appearance: none;
    appearance: none;
    font: inherit;
    color: inherit;
    background: initial;
    border: initial;
    text-align: inherit;
    width: 100%;
}

#gamemenu .tick {
    /* The tick at the start of a menu item, or its unselected equivalent.
     * This is represented by an <input type="radio">, so we put some
     * effort into overriding the default style. */
    -moz-appearance: none;
    -webkit-appearance: none;
    appearance: none;
    margin: initial;
    font: inherit;
}

#gamemenu .tick::before {
    content: "\\2713";
}

#gamemenu .tick:not(:checked) {
    /* Tick for an unselected menu entry. */
    color: transparent;
}

#gamemenu li > div:after {
    /* Downward arrow for submenu headings at the top level. */
    content: url("data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20width='10'%20height='10'%3E%3Cpolygon%20points='0,5,10,5,5,10'/%3E%3C/svg%3E");
    margin-left: 0.5em;
}

#gamemenu li li > div:after {
    /* Rightward arrow marker for submenus on lower-level menus. */
    content: url("data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20width='10'%20height='10'%3E%3Cpolygon%20points='0,0,10,5,0,10'/%3E%3C/svg%3E");
    float: right;
}


#statusbar {
    overflow: hidden;
    text-align: left;
    white-space: nowrap;
    text-overflow: ellipsis;
    background: #d8d8d8;
    border-left: 2px solid #c8c8c8;
    border-top: 2px solid #c8c8c8;
    border-right: 2px solid #e8e8e8;
    border-bottom: 2px solid #e8e8e8;
    height: 1.2em;
}

#dlgdimmer {
    width: 100%;
    height: 100%;
    background: #000000;
    position: fixed;
    opacity: 0.3;
    left: 0;
    top: 0;
    z-index: 99;
}

#dlgform {
    width: 66.6667vw;
    opacity: 1;
    background: #ffffff;
    color: #000000;
    position: absolute;
    border: 2px solid black;
    padding: 20px;
    top: 10vh;
    left: 16.6667vw;
    z-index: 100;
}

#dlgform h2 {
    margin-top: 0px;
}

#resizehandle {
    position: absolute;
    z-index: 1;
    bottom: 0;
    right: 0;
    cursor: se-resize;
}

#resizable {
    position: relative;
    margin: 0 auto;
}

#puzzlecanvas {
    display: block;
    width: 100%;
    /* This sets the font that will be used in the puzzle. */
    font-family: sans-serif;
}

#apology {
    padding: 0 1em 0 1em;
    margin: 1em;
    border: 2px solid red;
}

.apology-title {
    text-align: center;
}

\@media print {
    /* Interactive controls should be hidden when printing. */
    #gamemenu, #resizehandle { display: none; }
}
</style>
</head>
<body>
<h1 align=center>${puzzlename}</h1>
${unfinishedheading}
<h2 align=center>from Simon Tatham's Portable Puzzle Collection</h2>

${unfinishedpara}

<hr>
<main id="puzzle" style="display: none">
<form id="gamemenu"><ul>
  <li><div tabindex="0">Game<ul>
    <li><button type="button" id="specific">Enter game ID...</button></li>
    <li><button type="button" id="random">Enter random seed...</button></li>
    <li><button type="button" id="save">Download save file...</button></li>
    <li><button type="button" id="load">Upload save file...</button></li>
  </ul></div></li>
  <li><div tabindex="0">Type<ul role="menu" id="gametype"></ul></div></li>
  <li role="separator"></li>
  <li><button type="button" id="new">
    New<span class="verbiage"> game</span>
  </button></li>
  <li><button type="button" id="restart">
    Restart<span class="verbiage"> game</span>
  </button></li>
  <li><button type="button" id="undo">
    Undo<span class="verbiage"> move</span>
  </button></li>
  <li><button type="button" id="redo">
    Redo<span class="verbiage"> move</span>
  </button></li>
  <li><button type="button" id="solve">
    Solve<span class="verbiage"> game</span>
  </button></li>
</ul></form>
  <div id="resizable">
  <canvas id="puzzlecanvas" tabindex="0"></canvas>
  <div id="statusbar"></div>
  <img id="resizehandle" alt="resize"
       title="Drag to resize the puzzle. Right-click to restore the default size."
       src="data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20width='10'%20height='10'%3E%3Cpath%20d='M8.5,1.5l-7,7m7,-4l-4,4m4,-1l-1,1'%20stroke='black'%20stroke-linecap='round'/%3E%3C/svg%3E">
  </div>
  <p>
    Link to this puzzle:
    <a id="permalink-desc">by game ID</a>
    <a id="permalink-seed">by random seed</a>
  </p>
</main>
<div id="apology">
<p class="apology-title">If you've been reading this message for more
than a second or two, then <strong>this WebAssembly puzzle doesn't
seem to be working in your web browser</strong>. Sorry!</p>
<p>
<details>
<summary>More information</summary>
<p>Most modern browsers should support WASM. I've had reports of success from:
<ul>
<li>Firefox 87.0</li>
<li>Chrome 89.0.4389.114</li>
<li>Safari 14.0.3 (16610.4.3.1.7)</li>
<li>Edge 89.0.774.68</li>
<li>Opera 75.0.3969.149</li>
</ul></p>
<p>If this puzzle isn't working in one of those browsers (or a later
version), check to see whether you have any local configuration that
might have turned off WebAssembly completely, or some kind of blocking
software that might have prevented the WASM code from being downloaded
in the first place.</p>
<p>(For example, in Firefox, it's possible to turn off WASM completely
by setting <code>javascript.options.wasm</code> to <code>false</code>
in the <code>about:config</code> interface. If you've done that, or
something analogous in another browser, this puzzle won't run.)</p>
<p>In other browsers, the problem might be that WebAssembly isn't
supported at all (for example, Internet Explorer 11), or that a
browser update is needed.</p>
<p>If you think that your browser <em>should</em> support WebAssembly,
but this puzzle still isn't running, then please report the problem,
including <strong>as much diagnostic information as you can
find</strong>.</p>
<p>In particular, try opening your browser's Javascript error console
and then reloading this page, and tell me if it reports any error
messages.</p>
<p>Also, if your browser has a network diagnostic tab, try the same
experiment, to make sure it is successfully loading both of the
auxiliary files <code>${filename}.js</code> and
<code>${filename}.wasm</code>.</p>
</details>
</p>
</div>
<hr>

${instructions}

${links}

${footer}
</body>
</html>
EOF

    close $outpage;
}
