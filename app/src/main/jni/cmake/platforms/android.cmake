set(platform_common_sources android.c)
set(build_individual_puzzles FALSE)
set(build_cli_programs FALSE)
set(build_gui_programs FALSE)

add_compile_definitions(ANDROID COMBINED SMALL_SCREEN STYLUS_BASED NO_PRINTING VIVID_COLOURS)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wwrite-strings -std=c99 -pedantic -Werror")

function(get_platform_puzzle_extra_source_files OUTVAR NAME)
  set(${OUTVAR} PARENT_SCOPE)
endfunction()

function(set_platform_puzzle_target_properties NAME TARGET)
endfunction()
function(set_platform_gui_target_properties TARGET)
endfunction()

function(build_platform_extras)
  write_generated_games_header()

  add_library(puzzles SHARED list.c ${puzzle_sources})
  # TODO target_include_directories leads to failure to find generated-games.h, work out why and reinstate
  include_directories(${generated_include_dir})
  target_link_libraries(puzzles common)

  # Only executables with library-ish filenames are included in the APK and unpacked on devices
  add_executable(libpuzzlesgen.so "executable/android-gen.c")
  target_link_libraries(libpuzzlesgen.so puzzles)
endfunction()
