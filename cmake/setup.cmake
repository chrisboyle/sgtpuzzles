set(PUZZLES_ENABLE_UNFINISHED ""
  CACHE STRING "List of puzzles in the 'unfinished' subdirectory \
to build as if official (separated by ';')")

set(build_individual_puzzles TRUE)
set(build_cli_programs TRUE)
set(build_gui_programs TRUE)
set(build_icons FALSE)
set(need_c_icons FALSE)

# Include one of platforms/*.cmake to define platform-specific stuff.
# Each of these is expected to:
#  - define get_platform_puzzle_extra_source_files(), used below
#  - define set_platform_puzzle_target_properties(), used below
#  - define build_platform_extras(), called from the top-level CMakeLists.txt
#  - override the above build_* settings, if necessary
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  include(cmake/platforms/windows.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  include(cmake/platforms/osx.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES "NestedVM")
  include(cmake/platforms/nestedvm.cmake)
elseif(CMAKE_C_COMPILER MATCHES "emcc")
  include(cmake/platforms/emscripten.cmake)
else() # assume Unix
  include(cmake/platforms/unix.cmake)
endif()

# Accumulate lists of the puzzles' bare names and source file
# locations, for use in build_platform_extras() implementations when
# they want to build things based on all the puzzles at once.
set(puzzle_names)
set(puzzle_sources)

include(icons/icons.cmake)

# The main function called from the top-level CMakeLists.txt to define
# each puzzle.
function(puzzle NAME)
  cmake_parse_arguments(OPT
    "" "DISPLAYNAME;DESCRIPTION;OBJECTIVE;WINDOWS_EXE_NAME" "" ${ARGN})

  if(NOT DEFINED OPT_WINDOWS_EXE_NAME)
    set(OPT_WINDOWS_EXE_NAME ${NAME})
  endif()

  if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(EXENAME ${OPT_WINDOWS_EXE_NAME})
  else()
    set(EXENAME ${NAME})
  endif()

  set(exename_${NAME} ${EXENAME} PARENT_SCOPE)
  set(displayname_${NAME} ${OPT_DISPLAYNAME} PARENT_SCOPE)
  set(description_${NAME} ${OPT_DESCRIPTION} PARENT_SCOPE)
  set(objective_${NAME} ${OPT_OBJECTIVE} PARENT_SCOPE)

  set(official TRUE)
  if(NAME STREQUAL nullgame)
    # nullgame is not a playable puzzle; it has to be built (to prove
    # it still can build), but not installed, or included in the main
    # list of puzzles, or compiled into all-in-one binaries, etc. In
    # other words, it's not "officially" part of the puzzle
    # collection.
    set(official FALSE)
  endif()
  if(${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR}/unfinished)
    # The same goes for puzzles in the 'unfinished' subdirectory,
    # although we make an exception if configured to on the command
    # line.
    list(FIND PUZZLES_ENABLE_UNFINISHED ${NAME} enable_this_one)
    if(enable_this_one EQUAL -1)
      set(official FALSE)
    endif()
  endif()

  if (official)
    set(puzzle_names ${puzzle_names} ${NAME} PARENT_SCOPE)
    set(puzzle_sources ${puzzle_sources} ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.c PARENT_SCOPE)
  endif()

  get_platform_puzzle_extra_source_files(extra_files ${NAME})

  if (build_individual_puzzles)
    add_executable(${EXENAME} ${NAME}.c ${extra_files})
    target_link_libraries(${EXENAME}
      common ${platform_gui_libs} ${platform_libs})
    set_property(TARGET ${EXENAME} PROPERTY exename ${EXENAME})
    set_property(TARGET ${EXENAME} PROPERTY displayname ${OPT_DISPLAYNAME})
    set_property(TARGET ${EXENAME} PROPERTY description ${OPT_DESCRIPTION})
    set_property(TARGET ${EXENAME} PROPERTY objective ${OPT_OBJECTIVE})
    set_property(TARGET ${EXENAME} PROPERTY official ${official})
    set_platform_puzzle_target_properties(${NAME} ${EXENAME})
    set_platform_gui_target_properties(${EXENAME})
  endif()
endfunction()

# The main function called from the top-level CMakeLists.txt to define
# a command-line helper tool.
function(cliprogram NAME)
  cmake_parse_arguments(OPT
    "" "COMPILE_DEFINITIONS" "" ${ARGN})

  if(build_cli_programs)
    add_executable(${NAME} ${CMAKE_SOURCE_DIR}/nullfe.c
      ${OPT_UNPARSED_ARGUMENTS})
    target_link_libraries(${NAME} common ${platform_libs})
    if(OPT_COMPILE_DEFINITIONS)
      target_compile_definitions(${NAME} PRIVATE ${OPT_COMPILE_DEFINITIONS})
    endif()
  endif()
endfunction()

# Similar to cliprogram, but builds a GUI helper tool, linked against
# the normal puzzle frontend.
function(guiprogram NAME)
  cmake_parse_arguments(OPT
    "" "COMPILE_DEFINITIONS" "" ${ARGN})

  if(build_gui_programs)
    get_platform_puzzle_extra_source_files(extra_files nullgame)
    add_executable(${NAME} ${OPT_UNPARSED_ARGUMENTS} ${extra_files})
    target_link_libraries(${NAME}
      common ${platform_gui_libs} ${platform_libs})
    if(OPT_COMPILE_DEFINITIONS)
      target_compile_definitions(${NAME} PRIVATE ${OPT_COMPILE_DEFINITIONS})
    endif()
    set_platform_gui_target_properties(${NAME})
  endif()
endfunction()

# A small wrapper around cliprogram, taking advantage of the common
# formula that puzzle 'foo' often comes with 'foosolver'.
function(solver NAME)
  cliprogram(${NAME}solver ${puzzle_src_prefix}${NAME}.c ${ARGN}
    COMPILE_DEFINITIONS STANDALONE_SOLVER)
endfunction()

function(write_generated_games_header)
  set(generated_include_dir ${CMAKE_CURRENT_BINARY_DIR}/include)
  set(generated_include_dir ${generated_include_dir} PARENT_SCOPE)

  file(MAKE_DIRECTORY ${generated_include_dir})
  file(WRITE ${generated_include_dir}/generated-games.h "")
  list(SORT puzzle_names)
  foreach(name ${puzzle_names})
    file(APPEND ${generated_include_dir}/generated-games.h "GAME(${name})\n")
  endforeach()
endfunction()

# This has to be run from the unfinished subdirectory, so that the
# updates to puzzle_names etc will be propagated to the top-level scope.
macro(export_variables_to_parent_scope)
  set(puzzle_names ${puzzle_names} PARENT_SCOPE)
  set(puzzle_sources ${puzzle_sources} PARENT_SCOPE)
  foreach(name ${puzzle_names})
    set(exename_${name} ${exename_${name}} PARENT_SCOPE)
    set(displayname_${name} ${displayname_${name}} PARENT_SCOPE)
    set(description_${name} ${description_${name}} PARENT_SCOPE)
    set(objective_${name} ${objective_${name}} PARENT_SCOPE)
  endforeach()
endmacro()
