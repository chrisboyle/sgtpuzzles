puzzle(group
  DISPLAYNAME "Group"
  DESCRIPTION "Group theory puzzle"
  OBJECTIVE "Complete the unfinished Cayley table of a group.")
solver(group ${CMAKE_SOURCE_DIR}/latin.c)

puzzle(separate
  DISPLAYNAME "Separate"
  DESCRIPTION "Rectangle-dividing puzzle"
  OBJECTIVE "Partition the grid into regions containing one of each letter.")

puzzle(slide
  DISPLAYNAME "Slide"
  DESCRIPTION "Sliding block puzzle"
  OBJECTIVE "Slide the blocks to let the key block out.")
solver(slide)

puzzle(sokoban
  DISPLAYNAME "Sokoban"
  DESCRIPTION "Barrel-pushing puzzle"
  OBJECTIVE "Push all the barrels into the target squares.")

# These unfinished programs don't even have the structure of a puzzle
# game yet; they're just command-line programs containing test
# implementations of some of the needed functionality.

cliprogram(numgame numgame.c)

cliprogram(path path.c COMPILE_DEFINITIONS TEST_GEN)

export_variables_to_parent_scope()
