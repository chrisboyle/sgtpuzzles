cmake_minimum_required(VERSION 3.5)

project(puzzles
  LANGUAGES C)

include(cmake/setup.cmake)

add_library(common
  combi.c divvy.c drawing.c dsf.c findloop.c grid.c latin.c
  laydomino.c loopgen.c malloc.c matching.c midend.c misc.c penrose.c
  ps.c random.c sort.c tdq.c tree234.c version.c
  ${platform_common_sources})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

puzzle(blackbox
  DISPLAYNAME "Black Box"
  DESCRIPTION "Ball-finding puzzle"
  OBJECTIVE "Find the hidden balls in the box by bouncing laser beams \
off them.")

puzzle(bridges
  DISPLAYNAME "Bridges"
  DESCRIPTION "Bridge-placing puzzle"
  OBJECTIVE "Connect all the islands with a network of bridges.")

puzzle(cube
  DISPLAYNAME "Cube"
  DESCRIPTION "Rolling cube puzzle"
  OBJECTIVE "Pick up all the blue squares by rolling the cube over them.")

puzzle(dominosa
  DISPLAYNAME "Dominosa"
  DESCRIPTION "Domino tiling puzzle"
  OBJECTIVE "Tile the rectangle with a full set of dominoes.")
solver(dominosa)

puzzle(fifteen
  DISPLAYNAME "Fifteen"
  DESCRIPTION "Sliding block puzzle"
  OBJECTIVE "Slide the tiles around to arrange them into order.")
solver(fifteen)

puzzle(filling
  DISPLAYNAME "Filling"
  DESCRIPTION "Polyomino puzzle"
  OBJECTIVE "Mark every square with the area of its containing region.")
solver(filling)

puzzle(flip
  DISPLAYNAME "Flip"
  DESCRIPTION "Tile inversion puzzle"
  OBJECTIVE "Flip groups of squares to light them all up at once.")

puzzle(flood
  DISPLAYNAME "Flood"
  DESCRIPTION "Flood-filling puzzle"
  OBJECTIVE "Turn the grid the same colour in as few flood fills as possible.")

puzzle(galaxies
  DISPLAYNAME "Galaxies"
  DESCRIPTION "Symmetric polyomino puzzle"
  OBJECTIVE "Divide the grid into rotationally symmetric regions each \
centred on a dot.")
solver(galaxies)
cliprogram(galaxiespicture galaxies.c
  COMPILE_DEFINITIONS STANDALONE_PICTURE_GENERATOR)
guiprogram(galaxieseditor galaxies.c
  COMPILE_DEFINITIONS EDITOR)

puzzle(guess
  DISPLAYNAME "Guess"
  DESCRIPTION "Combination-guessing puzzle"
  OBJECTIVE "Guess the hidden combination of colours.")

puzzle(inertia
  DISPLAYNAME "Inertia"
  DESCRIPTION "Gem-collecting puzzle"
  OBJECTIVE "Collect all the gems without running into any of the mines.")

puzzle(keen
  DISPLAYNAME "Keen"
  DESCRIPTION "Arithmetic Latin square puzzle"
  OBJECTIVE "Complete the latin square in accordance with the \
arithmetic clues.")
solver(keen latin.c)

puzzle(lightup
  DISPLAYNAME "Light Up"
  DESCRIPTION "Light-bulb placing puzzle"
  OBJECTIVE "Place bulbs to light up all the squares.")
solver(lightup)

puzzle(loopy
  DISPLAYNAME "Loopy"
  DESCRIPTION "Loop-drawing puzzle"
  OBJECTIVE "Draw a single closed loop, given clues about number of \
adjacent edges.")
solver(loopy)

puzzle(magnets
  DISPLAYNAME "Magnets"
  DESCRIPTION "Magnet-placing puzzle"
  OBJECTIVE "Place magnets to satisfy the clues and avoid like poles \
touching.")
solver(magnets)

puzzle(map
  DISPLAYNAME "Map"
  DESCRIPTION "Map-colouring puzzle"
  OBJECTIVE "Colour the map so that adjacent regions are never the \
same colour.")
solver(map)

puzzle(mines
  DISPLAYNAME "Mines"
  DESCRIPTION "Mine-finding puzzle"
  OBJECTIVE "Find all the mines without treading on any of them.")
cliprogram(mineobfusc mines.c COMPILE_DEFINITIONS STANDALONE_OBFUSCATOR)

puzzle(mosaic
  DISPLAYNAME "Mosaic"
  DESCRIPTION "Grid-filling puzzle"
  OBJECTIVE "Fill in the grid given clues about number of \
nearby black squares.")

puzzle(net
  # The Windows Net shouldn't be called 'net.exe', since Windows
  # already has a reasonably important utility program by that name!
  WINDOWS_EXE_NAME netgame

  DISPLAYNAME "Net"
  DESCRIPTION "Network jigsaw puzzle"
  OBJECTIVE "Rotate each tile to reassemble the network.")

puzzle(netslide
  DISPLAYNAME "Netslide"
  DESCRIPTION "Toroidal sliding network puzzle"
  OBJECTIVE "Slide a row at a time to reassemble the network.")

puzzle(nullgame)

puzzle(palisade
  DISPLAYNAME "Palisade"
  DESCRIPTION "Grid-division puzzle"
  OBJECTIVE "Divide the grid into equal-sized areas in accordance with\
 the clues.")

puzzle(pattern
  DISPLAYNAME "Pattern"
  DESCRIPTION "Pattern puzzle"
  OBJECTIVE "Fill in the pattern in the grid, given only the lengths \
of runs of black squares.")
solver(pattern)
cliprogram(patternpicture pattern.c
  COMPILE_DEFINITIONS STANDALONE_PICTURE_GENERATOR)

puzzle(pearl
  DISPLAYNAME "Pearl"
  DESCRIPTION "Loop-drawing puzzle"
  OBJECTIVE "Draw a single closed loop, given clues about corner and \
straight squares.")
solver(pearl)
cliprogram(pearlbench pearl.c COMPILE_DEFINITIONS STANDALONE_SOLVER)

puzzle(pegs
  DISPLAYNAME "Pegs"
  DESCRIPTION "Peg solitaire puzzle"
  OBJECTIVE "Jump pegs over each other to remove all but one.")

puzzle(range
  DISPLAYNAME "Range"
  DESCRIPTION "Visible-distance puzzle"
  OBJECTIVE "Place black squares to limit the visible distance from \
each numbered cell.")

puzzle(rect
  DISPLAYNAME "Rectangles"
  DESCRIPTION "Rectangles puzzle"
  OBJECTIVE "Divide the grid into rectangles with areas equal to the \
numbers.")

puzzle(samegame
  DISPLAYNAME "Same Game"
  DESCRIPTION "Block-clearing puzzle"
  OBJECTIVE "Clear the grid by removing touching groups of the same \
colour squares.")

puzzle(signpost
  DISPLAYNAME "Signpost"
  DESCRIPTION "Square-connecting puzzle"
  OBJECTIVE "Connect the squares into a path following the arrows.")
solver(signpost)

puzzle(singles
  DISPLAYNAME "Singles"
  DESCRIPTION "Number-removing puzzle"
  OBJECTIVE "Black out the right set of duplicate numbers.")
solver(singles)

puzzle(sixteen
  DISPLAYNAME "Sixteen"
  DESCRIPTION "Toroidal sliding block puzzle"
  OBJECTIVE "Slide a row at a time to arrange the tiles into order.")

puzzle(slant
  DISPLAYNAME "Slant"
  DESCRIPTION "Maze-drawing puzzle"
  OBJECTIVE "Draw a maze of slanting lines that matches the clues.")
solver(slant)

puzzle(solo
  DISPLAYNAME "Solo"
  DESCRIPTION "Number placement puzzle"
  OBJECTIVE "Fill in the grid so that each row, column and square \
block contains one of every digit.")
solver(solo)

puzzle(tents
  DISPLAYNAME "Tents"
  DESCRIPTION "Tent-placing puzzle"
  OBJECTIVE "Place a tent next to each tree.")
solver(tents)

puzzle(towers
  DISPLAYNAME "Towers"
  DESCRIPTION "Tower-placing Latin square puzzle"
  OBJECTIVE "Complete the latin square of towers in accordance with \
the clues.")
solver(towers latin.c)

puzzle(tracks
  DISPLAYNAME "Tracks"
  DESCRIPTION "Path-finding railway track puzzle"
  OBJECTIVE "Fill in the railway track according to the clues.")
solver(tracks)

puzzle(twiddle
  DISPLAYNAME "Twiddle"
  DESCRIPTION "Rotational sliding block puzzle"
  OBJECTIVE "Rotate the tiles around themselves to arrange them into order.")

puzzle(undead
  DISPLAYNAME "Undead"
  DESCRIPTION "Monster-placing puzzle"
  OBJECTIVE "Place ghosts, vampires and zombies so that the right \
numbers of them can be seen in mirrors.")

puzzle(unequal
  DISPLAYNAME "Unequal"
  DESCRIPTION "Latin square puzzle"
  OBJECTIVE "Complete the latin square in accordance with the > signs.")
solver(unequal latin.c)

puzzle(unruly
  DISPLAYNAME "Unruly"
  DESCRIPTION "Black and white grid puzzle"
  OBJECTIVE "Fill in the black and white grid to avoid runs of three.")
solver(unruly)

puzzle(untangle
  DISPLAYNAME "Untangle"
  DESCRIPTION "Planar graph layout puzzle"
  OBJECTIVE "Reposition the points so that the lines do not cross.")

add_subdirectory(unfinished)

cliprogram(obfusc obfusc.c)
cliprogram(latincheck latin.c COMPILE_DEFINITIONS STANDALONE_LATIN_TEST)
cliprogram(matching matching.c COMPILE_DEFINITIONS STANDALONE_MATCHING_TEST)
cliprogram(combi combi.c COMPILE_DEFINITIONS STANDALONE_COMBI_TEST)
cliprogram(divvy divvy.c COMPILE_DEFINITIONS TESTMODE)
cliprogram(penrose-test penrose.c COMPILE_DEFINITIONS TEST_PENROSE)
cliprogram(penrose-vector-test penrose.c COMPILE_DEFINITIONS TEST_VECTORS)
cliprogram(sort-test sort.c COMPILE_DEFINITIONS SORT_TEST)
cliprogram(tree234-test tree234.c COMPILE_DEFINITIONS TEST)

build_platform_extras()
