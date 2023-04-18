#!/usr/bin/perl

use strict;
use warnings;

@ARGV == 2 or die "usage: apppage.pl <name> <displayname>";
my ($name, $displayname) = @ARGV;

print <<EOF;
<!DOCTYPE html>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=ASCII" />
<meta name="theme-color" content="rgb(50,50,50)" />    
<title>${displayname}</title>
<!-- The KaiAds scripts are only present in Kai Store versions. -->
<script src="kaiads.v5.min.js"></script>
<script defer type="text/javascript" src="${name}.js"></script>
<script defer type="text/javascript" src="kaiads-glue.js"></script>
<!-- Override some defaults for small screens -->
<script id="environment" type="application/json">
{ "PATTERN_DEFAULT": "10x10",
  "PUZZLES_ALLOW_CUSTOM": false,
  "PUZZLES_SHOW_CURSOR": true }
</script>
<style class="text/css">
body {
    margin: 0;
    display: flex;
    position: fixed;
    width: 100%;
    top: 0;
    bottom: 30px;
    font-family: "Open Sans", sans-serif;
    font-size: 17px;
}

/* Top-level form for the game menu */
#gamemenu {
    /* Add a little mild text formatting */
    font-weight: bold;
    font-size: 14px;
 }

/* Inside that form, the main menu bar and every submenu inside it is a <ul> */
#gamemenu ul {
    list-style: none;  /* get rid of the normal unordered-list bullets */
    display: flex;
    margin: 0;
    /* Compensate for the negative margins on menu items by adding a
     * little bit of padding so that the borders of the items don't protrude
     * beyond the menu. */
    padding: 0.5px;
    /* Switch to vertical stacking, for drop-down submenus */
    flex-direction: column;
    /* We must specify an explicit background colour for submenus, because
     * they must be opaque (don't want other page contents showing through
     * them). */
    background: white;
}

/* Individual menu items are <li> elements within such a <ul> */
#gamemenu li {
    /* Suppress the text-selection I-beam pointer */
    cursor: default;
    /* Surround each menu item with a border. */
    border: 1px solid rgb(180,180,180);
    /* Arrange that the borders of each item overlap the ones next to it. */
    margin: -0.5px;
}

#gamemenu ul li[role=separator] {
    color: transparent;
    border: 0;
}

/* The interactive contents of menu items are their child elements. */
#gamemenu li > * {
    padding: 0.2em 0.75em;
    margin: 0;
    display: block;
}


#gamemenu :disabled {
    /* Grey out disabled buttons */
    color: rgba(0,0,0,0.5);
}

/* #gamemenu li > :hover:not(:disabled), */
#gamemenu li > .focus-within {
    /* When the mouse is over a menu item, highlight it */
    background-color: rgba(0,0,0,0.3);
}

.transient {
    /* When they are displayed, they are positioned immediately above
     * their parent <li>, and with the left edge aligning */
    position: fixed;
    bottom: 30px;
    max-height: calc(100vh - 30px);
    left: 100%;
    transition: left 0.1s;
    box-sizing: border-box;
    width: 100vw;
    overflow: auto;
    /* And make sure they appear in front. */
    z-index: 50;
}

.transient.focus-within {
    /* Once a menu is actually focussed, bring it on screen. */
    left: 0;
    /* Hiding what's behind. */
    box-shadow: 0 0 1em 0 rgba(0, 0, 0, 0.8);
}

/* #gamemenu :hover > ul, */
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
    border-radius: initial;
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

#gamemenu li > div::after {
    content: url("data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20width='10'%20height='10'%3E%3Cpolygon%20points='0,0,10,5,0,10'/%3E%3C/svg%3E");
    float: right;
}

#puzzle {
    background: var(--puzzle-background, #e6e6e6);
    flex: 1 1 auto;
    flex-direction: column;
    align-items: center;
    display: flex;
    width: 100%
}

#statusbar {
    overflow: hidden;
    text-align: left;
    white-space: nowrap;
    text-overflow: ellipsis;
    line-height: 1;
    background: #d8d8d8;
    border-left: 2px solid #c8c8c8;
    border-top: 2px solid #c8c8c8;
    border-right: 2px solid #e8e8e8;
    border-bottom: 2px solid #e8e8e8;
    height: 1em;
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

#puzzlecanvascontain {
    flex: 1 1 auto;
    display: flex;
    justify-content: center;
    align-items: center;
    min-width: 0;
    min-height: 0;
}

#puzzlecanvas {
    max-width: 100%;
    max-height: 100%;
    background-color: white;
    font-weight: 600;
}

#puzzlecanvas:focus {
    /* The focus will be here iff there's nothing else on
     * screen that can be focused, so the outline is
     * redundant. */
    outline: none;
}

#puzzle > div {
    width: 100%;
}

.softkey {
    position: fixed;
    left: 0;
    right: 0;
    bottom: 0;
    height: 30px;
    font-weight: 600;
    font-size: 14px;
    line-height: 1;
    white-space: nowrap;
    background: rgb(50,50,50);
    color: white;
    z-index: 150;
}

:not(.focus-within) > .softkey {
    display: none;
}

.softkey > * {
    position: absolute;
    padding: 8px;
}

.lsk {
    left: 0;
    right: 70%;
    text-align: left;
    padding-right: 0;
}

.csk {
    left: 30%;
    right: 30%;
    text-align: center;
    text-transform: uppercase;
    padding-left: 0;
    padding-right: 0;
}

.rsk {
    right: 0;
    left: 70%;
    text-align: right;
    padding-left: 0
}

</style>
</head>
<body>
<div id="puzzle">
  <div id="puzzlecanvascontain">
    <canvas id="puzzlecanvas" width="1px" height="1px" tabindex="0">
    </canvas>
  </div>
  <div id="statusbar">
  </div>
  <div class="softkey">
    <div class="lsk"></div><div class="csk"></div>
    <div class="rsk">Menu</div></div>
</div>
<form id="gamemenu" class="transient">
 <ul>
<!--
  <li><div tabindex="0">Game<ul class="transient">
    <li><button type="button" id="specific">Enter game ID...</button></li>
    <li><button type="button" id="random">Enter random seed...</button></li>
    <li><button type="button" id="save">Download save file...</button></li>
    <li><button type="button" id="load">Upload save file...</button></li>
  </ul></div></li>
-->
  <li><div tabindex="0">Type<ul id="gametype" class="transient"></ul></div></li>
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
  <li><a target="_blank" href="help/en/${name}.html#${name}">
    Instructions
  </a></li>
  <li><a target="_blank" href="help/en/index.html">
    Full manual
  </a></li>
 </ul>
 <div class="softkey">
   <div class="csk">Select</div>
   <div class="rsk">Dismiss</div>
 </div>
</form>
</body>
</html>
EOF
