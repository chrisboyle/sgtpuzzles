set(platform_common_sources windows.c printing.c)

set(platform_gui_libs
  user32.lib gdi32.lib comctl32.lib comdlg32.lib winspool.lib)

set(platform_libs)

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
  # Turn off some warnings that I've just found too noisy.
  #
  #  - 4244, 4267: "possible loss of data" when narrowing an integer
  #    type (separate warning numbers for initialisers and
  #    assignments). Every time I spot-check instances of this, they
  #    turn out to be sensible (e.g. something was already checked, or
  #    was assigned from a previous variable that must have been in
  #    range). I don't think putting a warning-suppression idiom at
  #    every one of these sites would improve code legibility.
  #
  #  - 4018: "signed/unsigned mismatch" in integer comparison. Again,
  #    comes up a lot, and generally my spot checks make it look as if
  #    it's OK.
  #
  #  - 4146: applying unary '-' to an unsigned type. This happens once,
  #    in Untangle, and it's on purpose - but I haven't found any idiom
  #    at the point of use that reassures the compiler that I meant it.
  #
  #  - 4305: truncation from double to float. We use float all the time
  #    in this code base, and truncations from double are fine.

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} \
/wd4244 /wd4267 /wd4018 /wd4146 /wd4305")
endif()

function(get_platform_puzzle_extra_source_files OUTVAR NAME AUXILIARY)
  if(NOT AUXILIARY)
    set(${OUTVAR} ${CMAKE_SOURCE_DIR}/puzzles.rc PARENT_SCOPE)
  endif()
endfunction()

function(set_platform_gui_target_properties TARGET)
  set_target_properties(${TARGET} PROPERTIES WIN32_EXECUTABLE ON)
endfunction()

# Escape a string to make it into a C string literal. Used for textual
# #defines to put into VERSIONINFO resources.
function(c_string_escape outvar value)
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  set("${outvar}" "${value}" PARENT_SCOPE)
endfunction()

# Set the character set for resource files, because we're going to use
# a copyright sign in ours.
if(CMAKE_C_COMPILER_ID MATCHES "MSVC" OR
   CMAKE_C_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
  set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /nologo /C1252")
else()
  set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -c1252")
endif()

# Get the first line of LICENCE to put into VERSIONINFO resources.
file(READ "${CMAKE_SOURCE_DIR}/LICENCE" LICENCE_TEXT)
set(copyright_regex "This software is copyright ([^\n]*[^\n.])\\.?\n")
string(REGEX MATCH "${copyright_regex}" COPYRIGHT_NOTICE "${LICENCE_TEXT}")
string(REGEX REPLACE "${copyright_regex}" "\\1"
  COPYRIGHT_NOTICE "${COPYRIGHT_NOTICE}")
c_string_escape(COPYRIGHT_NOTICE "Copyright ${COPYRIGHT_NOTICE}")
string(REGEX REPLACE "\\([Cc]\\)" "\\\\251"
  COPYRIGHT_NOTICE "${COPYRIGHT_NOTICE}")

function(set_platform_puzzle_target_properties NAME TARGET)
  if(DEFINED ICO_DIR AND EXISTS ${ICO_DIR}/${NAME}.ico)
    target_compile_definitions(${TARGET} PRIVATE ICON_FILE=\"${ICO_DIR}/${NAME}.ico\")
  endif()

  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/${NAME})
  c_string_escape(QUOTED_DESCRIPTION "${OPT_DESCRIPTION}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/include/${NAME}/gamedetails.h" "\
/* #defines for ${name}'s VERSIONINFO */
#define VERSIONINFO_GAMENAME \"${NAME}\"
#define VERSIONINFO_GAMEDESC \"${QUOTED_DESCRIPTION}\"
#define VERSIONINFO_EXENAME \"${TARGET}\"
#define VERSIONINFO_COPYRIGHT \"${COPYRIGHT_NOTICE}\"
")
  target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include/${NAME})
endfunction()

function(build_platform_extras)
  write_generated_games_header()

  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gamedesc.txt "")
  list(SORT puzzle_names)
  foreach(name ${puzzle_names})
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/gamedesc.txt "\
${name}:\
${exename_${name}}.exe:\
${displayname_${name}}:\
${description_${name}}:\
${objective_${name}}\n")
  endforeach()

  add_executable(puzzles windows.c list.c ${puzzle_sources})
  target_compile_definitions(puzzles PRIVATE COMBINED)
  target_include_directories(puzzles PRIVATE ${generated_include_dir})
  target_link_libraries(puzzles common ${platform_gui_libs} ${platform_libs})
  set_target_properties(puzzles PROPERTIES WIN32_EXECUTABLE ON)
endfunction()
