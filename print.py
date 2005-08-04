#!/usr/bin/env python

# This program accepts a series of newline-separated game IDs on
# stdin and formats them into PostScript to be printed out. You
# specify using command-line options which game the IDs are for,
# and how many you want per page.

# Supported games are those which are sensibly solvable using
# pencil and paper: Rectangles, Pattern, Solo, Net.

# Command-line syntax is
#
#     print.py <game-name> <format>
#
# <game-name> is one of `rect', `rectangles', `pattern', `solo',
# `net', `dominosa'. <format> is two numbers separated by an x:
# `2x3', for example, means two columns by three rows.
#
# The program will then read game IDs from stdin until it sees EOF,
# and generate as many PostScript pages on stdout as it needs.
#
# The resulting PostScript will automatically adapt itself to the
# size of the clip rectangle, so that the puzzles are sensibly
# distributed across whatever paper size you decide to use.

import sys
import string
import re

class Holder:
    pass

def psvprint(h, a):
    for i in xrange(len(a)):
	h.s = h.s + str(a[i])
	if i < len(a)-1:
	    h.s = h.s + " "
	else:
	    h.s = h.s + "\n"

def psprint(h, *a):
    psvprint(h, a)

def rect_format(s):
    # Parse the game ID.
    ret = Holder()
    ret.s = ""
    params, seed = string.split(s, ":")
    w, h = map(string.atoi, string.split(params, "x"))
    grid = []
    while len(seed) > 0:
	if seed[0] in '_'+string.lowercase:
	    if seed[0] in string.lowercase:
		grid.extend([-1] * (ord(seed[0]) - ord('a') + 1))
	    seed = seed[1:]
	elif seed[0] in string.digits:
	    ns = ""
	    while len(seed) > 0 and seed[0] in string.digits:
		ns = ns + seed[0]
		seed = seed[1:]
	    grid.append(string.atoi(ns))
    assert w * h == len(grid)
    # I'm going to arbitrarily choose to use 7pt text for the
    # numbers, and a 14pt grid pitch.
    textht = 7
    gridpitch = 14
    # Set up coordinate system.
    pw = gridpitch * w
    ph = gridpitch * h
    ret.coords = (pw/2, pw/2, ph/2, ph/2)
    psprint(ret, "%g %g translate" % (-ret.coords[0], -ret.coords[2]))
    # Draw the internal grid lines, _very_ thin (the player will
    # need to draw over them visibly).
    psprint(ret, "newpath 0.01 setlinewidth")
    for x in xrange(1,w):
	psprint(ret, "%g 0 moveto 0 %g rlineto" % (x * gridpitch, h * gridpitch))
    for y in xrange(1,h):
	psprint(ret, "0 %g moveto %g 0 rlineto" % (y * gridpitch, w * gridpitch))
    psprint(ret, "stroke")
    # Draw round the grid exterior, much thicker.
    psprint(ret, "newpath 1.5 setlinewidth")
    psprint(ret, "0 0 moveto 0 %g rlineto %g 0 rlineto 0 %g rlineto" % \
    (h * gridpitch, w * gridpitch, -h * gridpitch))
    psprint(ret, "closepath stroke")
    # And draw the numbers.
    psprint(ret, "/Helvetica findfont %g scalefont setfont" % textht)
    for y in xrange(h):
	for x in xrange(w):
	    n = grid[y*w+x]
	    if n > 0:
		psprint(ret, "%g %g (%d) ctshow" % \
		((x+0.5)*gridpitch, (h-y-0.5)*gridpitch, n))
    return ret.coords, ret.s

def net_format(s):
    # Parse the game ID.
    ret = Holder()
    ret.s = ""
    params, seed = string.split(s, ":")
    wrapping = 0
    if params[-1:] == "w":
	wrapping = 1
	params = params[:-1]
    w, h = map(string.atoi, string.split(params, "x"))
    grid = []
    hbarriers = []
    vbarriers = []
    while len(seed) > 0:
	n = string.atoi(seed[0], 16)
	seed = seed[1:]
	while len(seed) > 0 and seed[0] in 'hv':
	    x = len(grid) % w
	    y = len(grid) / w
	    if seed[0] == 'h':
		hbarriers.append((x, y+1))
	    else:
		vbarriers.append((x+1, y))
	    seed = seed[1:]
	grid.append(n)
    assert w * h == len(grid)
    # I'm going to arbitrarily choose a 24pt grid pitch.
    gridpitch = 24
    scale = 0.25
    bigoffset = 0.25
    smalloffset = 0.17
    squaresize = 0.25
    # Set up coordinate system.
    pw = gridpitch * w
    ph = gridpitch * h
    ret.coords = (pw/2, pw/2, ph/2, ph/2)
    psprint(ret, "%g %g translate" % (-ret.coords[0], -ret.coords[2]))
    # Draw the base grid lines.
    psprint(ret, "newpath 0.02 setlinewidth")
    for x in xrange(1,w):
	psprint(ret, "%g 0 moveto 0 %g rlineto" % (x * gridpitch, h * gridpitch))
    for y in xrange(1,h):
	psprint(ret, "0 %g moveto %g 0 rlineto" % (y * gridpitch, w * gridpitch))
    psprint(ret, "stroke")
    # Draw round the grid exterior.
    psprint(ret, "newpath")
    if not wrapping:
	psprint(ret, "2 setlinewidth")
    psprint(ret, "0 0 moveto 0 %g rlineto %g 0 rlineto 0 %g rlineto" % \
    (h * gridpitch, w * gridpitch, -h * gridpitch))
    psprint(ret, "closepath stroke")
    # Draw any barriers.
    psprint(ret, "newpath 2 setlinewidth 1 setlinecap")
    for x, y in hbarriers:
	psprint(ret, "%g %g moveto %g 0 rlineto" % \
	(x * gridpitch, (h - y) * gridpitch, gridpitch))
    for x, y in vbarriers:
	psprint(ret, "%g %g moveto 0 -%g rlineto" % \
	(x * gridpitch, (h - y) * gridpitch, gridpitch))
    psprint(ret, "stroke")
    # And draw the symbol in each box.
    for i in xrange(len(grid)):
	x = i % w
	y = i / w
	v = grid[i]
	# Rotate to canonical form.
	if v in (1,2,4,8):
	    v = 1
	elif v in (5,10):
	    v = 5
	elif v in (3,6,9,12):
	    v = 9
	elif v in (7,11,13,14):
	    v = 13
	# Centre on an area in the corner of the tile.
	psprint(ret, "gsave")
	if v & 4:
	    hoffset = bigoffset
	else:
	    hoffset = smalloffset
	if v & 2:
	    voffset = bigoffset
	else:
	    voffset = smalloffset
	psprint(ret, "%g %g translate" % \
	((x + hoffset) * gridpitch, (h - y - voffset) * gridpitch))
	psprint(ret, "%g dup scale" % (float(gridpitch) * scale / 2))
	psprint(ret, "newpath 0.07 setlinewidth")
	# Draw the radial lines.
	for dx, dy, z in ((1,0,1), (0,1,2), (-1,0,4), (0,-1,8)):
	    if v & z:
		psprint(ret, "0 0 moveto %d %d lineto" % (dx, dy))
	psprint(ret, "stroke")
	# Draw additional figures if desired.
	if v == 1:
	    # Endpoints have a little empty square at the centre.
	    psprint(ret, "newpath %g %g moveto 0 -%g rlineto" % \
	    (squaresize, squaresize, squaresize * 2))
	    psprint(ret, "-%g 0 rlineto 0 %g rlineto closepath fill" % \
	    (squaresize * 2, squaresize * 2))
	# Get back out of the centre section.
	psprint(ret, "grestore")
	# Draw the endpoint square in large in the middle.
	if v == 1:
	    psprint(ret, "gsave")
	    psprint(ret, "%g %g translate" % \
	    ((x + 0.5) * gridpitch, (h - y - 0.5) * gridpitch))
	    psprint(ret, "%g dup scale" % (float(gridpitch) / 2))
	    psprint(ret, "newpath %g %g moveto 0 -%g rlineto" % \
	    (squaresize, squaresize, squaresize * 2))
	    psprint(ret, "-%g 0 rlineto 0 %g rlineto closepath fill" % \
	    (squaresize * 2, squaresize * 2))
	    psprint(ret, "grestore")
    return ret.coords, ret.s

def pattern_format(s):
    ret = Holder()
    ret.s = ""
    # Parse the game ID.
    params, seed = string.split(s, ":")
    w, h = map(string.atoi, string.split(params, "x"))
    rowdata = map(lambda s: string.split(s, "."), string.split(seed, "/"))
    assert len(rowdata) == w+h
    # I'm going to arbitrarily choose to use 7pt text for the
    # numbers, and a 14pt grid pitch.
    textht = 7
    gridpitch = 14
    gutter = 8 # between the numbers and the grid
    # Find the maximum number of numbers in each dimension, to
    # determine the border size required.
    xborder = reduce(max, map(len, rowdata[w:]))
    yborder = reduce(max, map(len, rowdata[:w]))
    # Set up coordinate system. I'm going to put the origin at the
    # _top left_ of the grid, so that both sets of numbers get
    # drawn the same way.
    pw = (w + xborder) * gridpitch + gutter
    ph = (h + yborder) * gridpitch + gutter
    ret.coords = (xborder * gridpitch + gutter, w * gridpitch, \
    yborder * gridpitch + gutter, h * gridpitch)
    # Draw the internal grid lines. Every fifth one is thicker, as
    # a visual aid.
    psprint(ret, "newpath 0.1 setlinewidth")
    for x in xrange(1,w):
	if x % 5 != 0:
	    psprint(ret, "%g 0 moveto 0 %g rlineto" % (x * gridpitch, -h * gridpitch))
    for y in xrange(1,h):
	if y % 5 != 0:
	    psprint(ret, "0 %g moveto %g 0 rlineto" % (-y * gridpitch, w * gridpitch))
    psprint(ret, "stroke")
    psprint(ret, "newpath 0.75 setlinewidth")
    for x in xrange(5,w,5):
	psprint(ret, "%g 0 moveto 0 %g rlineto" % (x * gridpitch, -h * gridpitch))
    for y in xrange(5,h,5):
	psprint(ret, "0 %g moveto %g 0 rlineto" % (-y * gridpitch, w * gridpitch))
    psprint(ret, "stroke")
    # Draw round the grid exterior.
    psprint(ret, "newpath 1.5 setlinewidth")
    psprint(ret, "0 0 moveto 0 %g rlineto %g 0 rlineto 0 %g rlineto" % \
    (-h * gridpitch, w * gridpitch, h * gridpitch))
    psprint(ret, "closepath stroke")
    # And draw the numbers.
    psprint(ret, "/Helvetica findfont %g scalefont setfont" % textht)
    for i in range(w+h):
	ns = rowdata[i]
	if i < w:
	    xo = (i + 0.5) * gridpitch
	    yo = (gutter + 0.5 * gridpitch)
	else:
	    xo = -(gutter + 0.5 * gridpitch)
	    yo = ((i-w) + 0.5) * -gridpitch
	for j in range(len(ns)-1, -1, -1):
	    psprint(ret, "%g %g (%s) ctshow" % (xo, yo, ns[j]))
	    if i < w:
		yo = yo + gridpitch
	    else:
		xo = xo - gridpitch
    return ret.coords, ret.s

def solo_format(s):
    ret = Holder()
    ret.s = ""
    # Parse the game ID.
    params, seed = string.split(s, ":")
    c, r = map(string.atoi, string.split(params, "x"))
    cr = c*r
    grid = []
    while len(seed) > 0:
	if seed[0] in '_'+string.lowercase:
	    if seed[0] in string.lowercase:
		grid.extend([-1] * (ord(seed[0]) - ord('a') + 1))
	    seed = seed[1:]
	elif seed[0] in string.digits:
	    ns = ""
	    while len(seed) > 0 and seed[0] in string.digits:
		ns = ns + seed[0]
		seed = seed[1:]
	    grid.append(string.atoi(ns))
    assert cr * cr == len(grid)
    # I'm going to arbitrarily choose to use 9pt text for the
    # numbers, and a 16pt grid pitch.
    textht = 9
    gridpitch = 16
    # Set up coordinate system.
    pw = ph = gridpitch * cr
    ret.coords = (pw/2, pw/2, ph/2, ph/2)
    psprint(ret, "%g %g translate" % (-ret.coords[0], -ret.coords[2]))
    # Draw the thin internal grid lines.
    psprint(ret, "newpath 0.1 setlinewidth")
    for x in xrange(1,cr):
	if x % r != 0:
	    psprint(ret, "%g 0 moveto 0 %g rlineto" % (x * gridpitch, cr * gridpitch))
    for y in xrange(1,cr):
	if y % c != 0:
	    psprint(ret, "0 %g moveto %g 0 rlineto" % (y * gridpitch, cr * gridpitch))
    psprint(ret, "stroke")
    # Draw the thicker internal grid lines.
    psprint(ret, "newpath 1 setlinewidth")
    for x in xrange(r,cr,r):
	psprint(ret, "%g 0 moveto 0 %g rlineto" % (x * gridpitch, cr * gridpitch))
    for y in xrange(c,cr,c):
	psprint(ret, "0 %g moveto %g 0 rlineto" % (y * gridpitch, cr * gridpitch))
    psprint(ret, "stroke")
    # Draw round the grid exterior, thicker still.
    psprint(ret, "newpath 1.5 setlinewidth")
    psprint(ret, "0 0 moveto 0 %g rlineto %g 0 rlineto 0 %g rlineto" % \
    (cr * gridpitch, cr * gridpitch, -cr * gridpitch))
    psprint(ret, "closepath stroke")
    # And draw the numbers.
    psprint(ret, "/Helvetica findfont %g scalefont setfont" % textht)
    for y in xrange(cr):
	for x in xrange(cr):
	    n = grid[y*cr+x]
	    if n > 0:
		if n > 9:
		    s = chr(ord('a') + n - 10)
		else:
		    s = chr(ord('0') + n)
		psprint(ret, "%g %g (%s) ctshow" % \
		((x+0.5)*gridpitch, (cr-y-0.5)*gridpitch, s))
    return ret.coords, ret.s

def dominosa_format(s):
    ret = Holder()
    ret.s = ""
    params, seed = string.split(s, ":")
    n = string.atoi(params)
    w = n+2
    h = n+1
    grid = []
    while len(seed) > 0:
        if seed[0] == '[': # XXX
            d, seed = string.split(seed[1:], "]")
            grid.append(string.atoi(d))
        else:
            assert seed[0] in string.digits
            grid.append(string.atoi(seed[0:1]))
            seed = seed[1:]
    assert w*h == len(grid)
    # I'm going to arbitrarily choose to use 9pt text for the
    # numbers, and a 16pt grid pitch.
    textht = 9
    gridpitch = 16
    pw = gridpitch * w
    ph = gridpitch * h
    psprint(ret, "/Helvetica findfont %g scalefont setfont" % textht)
    ret.coords = (pw/2, pw/2, ph/2, ph/2)
    psprint(ret, "%g %g translate" % (-ret.coords[0], -ret.coords[2]))
    for y in xrange(h):
        for x in xrange(w):
            psprint(ret, "%g %g (%d) ctshow" % \
                ((x+0.5)*gridpitch, (h-y-0.5)*gridpitch, grid[y*w+x]))
    return ret.coords, ret.s

def slant_format(s):
    # Parse the game ID.
    ret = Holder()
    ret.s = ""
    params, seed = string.split(s, ":")
    w, h = map(string.atoi, string.split(params, "x"))
    W = w+1
    H = h+1
    grid = []
    while len(seed) > 0:
	if seed[0] in string.lowercase:
	    grid.extend([-1] * (ord(seed[0]) - ord('a') + 1))
	    seed = seed[1:]
	elif seed[0] in "01234":
	    grid.append(string.atoi(seed[0]))
	    seed = seed[1:]
    assert W * H == len(grid)
    # I'm going to arbitrarily choose to use 7pt text for the
    # numbers, and a 14pt grid pitch.
    textht = 7
    gridpitch = 14
    radius = textht * 2.0 / 3.0
    # Set up coordinate system.
    pw = gridpitch * w
    ph = gridpitch * h
    ret.coords = (pw/2, pw/2, ph/2, ph/2)
    psprint(ret, "%g %g translate" % (-ret.coords[0], -ret.coords[2]))
    # Draw round the grid exterior, thickly.
    psprint(ret, "newpath 1 setlinewidth")
    psprint(ret, "0 0 moveto 0 %g rlineto %g 0 rlineto 0 %g rlineto" % \
    (h * gridpitch, w * gridpitch, -h * gridpitch))
    psprint(ret, "closepath stroke")
    # Draw the internal grid lines, _very_ thin (the player will
    # need to draw over them visibly).
    psprint(ret, "newpath 0.01 setlinewidth")
    for x in xrange(1,w):
	psprint(ret, "%g 0 moveto 0 %g rlineto" % (x * gridpitch, h * gridpitch))
    for y in xrange(1,h):
	psprint(ret, "0 %g moveto %g 0 rlineto" % (y * gridpitch, w * gridpitch))
    psprint(ret, "stroke")
    # And draw the numbers.
    psprint(ret, "/Helvetica findfont %g scalefont setfont" % textht)
    for y in xrange(H):
	for x in xrange(W):
	    n = grid[y*W+x]
	    if n >= 0:
		psprint(ret, "newpath %g %g %g 0 360 arc" % \
		((x)*gridpitch, (h-y)*gridpitch, radius),
		"gsave 1 setgray fill grestore stroke")
		psprint(ret, "%g %g (%d) ctshow" % \
		((x)*gridpitch, (h-y)*gridpitch, n))
    return ret.coords, ret.s

def lightup_format(s):
    # Parse the game ID.
    ret = Holder()
    ret.s = ""
    params, seed = string.split(s, ":")
    w, h = map(string.atoi, string.split(params, "x"))
    grid = []
    while len(seed) > 0:
	if seed[0] in string.lowercase:
	    grid.extend([-2] * (ord(seed[0]) - ord('a') + 1))
	    seed = seed[1:]
	elif seed[0] == "B":
	    grid.append(-1)
	    seed = seed[1:]
	elif seed[0] in "01234":
	    grid.append(string.atoi(seed[0]))
	    seed = seed[1:]
    assert w * h == len(grid)
    # I'm going to arbitrarily choose to use 9pt text for the
    # numbers, and a 14pt grid pitch.
    textht = 10
    gridpitch = 14
    # Set up coordinate system.
    pw = gridpitch * w
    ph = gridpitch * h
    ret.coords = (pw/2, pw/2, ph/2, ph/2)
    psprint(ret, "%g %g translate" % (-ret.coords[0], -ret.coords[2]))
    # Draw round the grid exterior, thickly.
    psprint(ret, "newpath 1 setlinewidth")
    psprint(ret, "0 0 moveto 0 %g rlineto %g 0 rlineto 0 %g rlineto" % \
    (h * gridpitch, w * gridpitch, -h * gridpitch))
    psprint(ret, "closepath stroke")
    # Draw the internal grid lines.
    psprint(ret, "newpath 0.02 setlinewidth")
    for x in xrange(1,w):
	psprint(ret, "%g 0 moveto 0 %g rlineto" % (x * gridpitch, h * gridpitch))
    for y in xrange(1,h):
	psprint(ret, "0 %g moveto %g 0 rlineto" % (y * gridpitch, w * gridpitch))
    psprint(ret, "stroke")
    # And draw the black squares and numbers.
    psprint(ret, "/Helvetica-Bold findfont %g scalefont setfont" % textht)
    for y in xrange(h):
	for x in xrange(w):
	    n = grid[y*w+x]
	    if n >= -1:
		psprint(ret, ("newpath %g %g moveto 0 %g rlineto " +
		"%g 0 rlineto 0 %g rlineto closepath fill") % \
		((x)*gridpitch, (h-1-y)*gridpitch, gridpitch, gridpitch, \
		-gridpitch))
		if n >= 0:
		    psprint(ret, "gsave 1 setgray %g %g (%d) ctshow grestore" % \
		    ((x+0.5)*gridpitch, (h-y-0.5)*gridpitch, n))
    return ret.coords, ret.s

formatters = {
"net": net_format,
"rect": rect_format,
"rectangles": rect_format,
"pattern": pattern_format,
"solo": solo_format,
"dominosa": dominosa_format,
"slant": slant_format,
"lightup": lightup_format
}

if len(sys.argv) < 3:
    sys.stderr.write("print.py: expected two arguments (game and format)\n")
    sys.exit(1)

formatter = formatters.get(sys.argv[1], None)
if formatter == None:
    sys.stderr.write("print.py: unrecognised game name `%s'\n" % sys.argv[1])
    sys.exit(1)

try:
    format = map(string.atoi, string.split(sys.argv[2], "x"))
except ValueError, e:
    format = []
if len(format) != 2:
    sys.stderr.write("print.py: expected format such as `2x3' as second" \
    + " argument\n")
    sys.exit(1)

xx, yy = format
ppp = xx * yy # puzzles per page

ids = []
while 1:
    s = sys.stdin.readline()
    if s == "": break
    if s[-1:] == "\n": s = s[:-1]
    ids.append(s)

pages = int((len(ids) + ppp - 1) / ppp)

# Output initial DSC stuff.
print "%!PS-Adobe-3.0"
print "%%Creator: print.py from Simon Tatham's Puzzle Collection"
print "%%DocumentData: Clean7Bit"
print "%%LanguageLevel: 1"
print "%%Pages:", pages
print "%%DocumentNeededResources:"
print "%%+ font Helvetica"
print "%%DocumentSuppliedResources: procset Puzzles 0 0"
print "%%EndComments"
print "%%BeginProlog"
print "%%BeginResource: procset Puzzles 0 0"
print "/ctshow {"
print "  3 1 roll"
print "  newpath 0 0 moveto (X) true charpath flattenpath pathbbox"
print "  3 -1 roll add 2 div 3 1 roll pop pop sub moveto"
print "  dup stringwidth pop 0.5 mul neg 0 rmoveto show"
print "} bind def"
print "%%EndResource"
print "%%EndProlog"
print "%%BeginSetup"
print "%%IncludeResource: font Helvetica"
print "%%EndSetup"

# Now do each page.
puzzle_index = 0;

for i in xrange(1, pages+1):
    print "%%Page:", i, i
    print "save"

    # Do the drawing for each puzzle, giving a set of PS fragments
    # and bounding boxes.
    fragments = [['' for i in xrange(xx)] for i in xrange(yy)]
    lrbound = [(0,0) for i in xrange(xx)]
    tbbound = [(0,0) for i in xrange(yy)]

    for y in xrange(yy):
	for x in xrange(xx):
	    if puzzle_index >= len(ids):
		break
	    coords, frag = formatter(ids[puzzle_index])
	    fragments[y][x] = frag
	    lb, rb = lrbound[x]
	    lrbound[x] = (max(lb, coords[0]), max(rb, coords[1]))
	    tb, bb = tbbound[y]
	    tbbound[y] = (max(tb, coords[2]), max(bb, coords[3]))
	    puzzle_index = puzzle_index + 1

    # Now we know the sizes of everything, do the drawing in such a
    # way that we provide equal gutter space at the page edges and
    # between puzzle rows/columns.
    for y in xrange(yy):
	for x in xrange(xx):
	    if len(fragments[y][x]) > 0:
		print "gsave"
		print "clippath flattenpath pathbbox pop pop translate"
		print "clippath flattenpath pathbbox 4 2 roll pop pop"
		# Compute the total height of all puzzles, which
		# we'll use it to work out the amount of gutter
		# space below this puzzle.
		htotal = reduce(lambda a,b:a+b, map(lambda (a,b):a+b, tbbound), 0)
		# Now compute the total height of all puzzles
		# _below_ this one, plus the height-below-origin of
		# this one.
		hbelow = reduce(lambda a,b:a+b, map(lambda (a,b):a+b, tbbound[y+1:]), 0)
		hbelow = hbelow + tbbound[y][1]
		print "%g sub %d mul %d div %g add exch" % (htotal, yy-y, yy+1, hbelow)
		# Now do all the same computations for width,
		# except we need the total width of everything
		# _before_ this one since the coordinates work the
		# other way round.
		wtotal = reduce(lambda a,b:a+b, map(lambda (a,b):a+b, lrbound), 0)
		# Now compute the total height of all puzzles
		# _below_ this one, plus the height-below-origin of
		# this one.
		wleft = reduce(lambda a,b:a+b, map(lambda (a,b):a+b, lrbound[:x]), 0)
		wleft = wleft + lrbound[x][0]
		print "%g sub %d mul %d div %g add exch" % (wtotal, x+1, xx+1, wleft)
		print "translate"
		sys.stdout.write(fragments[y][x])
		print "grestore"

    print "restore showpage"

print "%%EOF"
