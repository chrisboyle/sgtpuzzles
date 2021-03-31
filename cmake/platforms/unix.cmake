find_package(PkgConfig REQUIRED)

set(PUZZLES_GTK_FOUND FALSE)
macro(try_gtk_package VER PACKAGENAME)
  if(NOT PUZZLES_GTK_FOUND AND
      (NOT DEFINED PUZZLES_GTK_VERSION OR
        PUZZLES_GTK_VERSION STREQUAL ${VER}))
    pkg_check_modules(GTK ${PACKAGENAME})
    if(GTK_FOUND)
      set(PUZZLES_GTK_FOUND TRUE)
    endif()
  endif()
endmacro()

try_gtk_package(3 gtk+-3.0)
try_gtk_package(2 gtk+-2.0)

if(NOT PUZZLES_GTK_FOUND)
  message(FATAL_ERROR "Unable to find any usable version of GTK.")
endif()

include_directories(${GTK_INCLUDE_DIRS})
link_directories(${GTK_LIBRARY_DIRS})

set(platform_common_sources gtk.c printing.c)
set(platform_gui_libs ${GTK_LIBRARIES})

set(platform_libs -lm)

set(build_icons TRUE)

if(DEFINED STRICT AND (CMAKE_C_COMPILER_ID MATCHES "GNU" OR
                       CMAKE_C_COMPILER_ID MATCHES "Clang"))
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wwrite-strings -std=c99 -pedantic -Werror")
endif()

function(get_platform_puzzle_extra_source_files OUTVAR NAME)
  if(build_icons AND EXISTS ${CMAKE_SOURCE_DIR}/icons/${NAME}.sav)
    # If we have the equipment to rebuild the puzzles' icon images
    # from scratch, do so. Then changes in the puzzle display code
    # will cause the icon to auto-update.
    build_icon(${NAME})
    set(c_icon_file ${CMAKE_BINARY_DIR}/icons/${NAME}-icon.c)
  elseif(EXISTS ${CMAKE_SOURCE_DIR}/icons/${NAME}-icon.c)
    # Failing that, use a pre-built icon file in the 'icons'
    # subdirectory, if there is one. (They don't exist in git, but the
    # distribution tarball will have pre-built them and put them in
    # there, so that users building from that can still have icons
    # even if they don't have the wherewithal to rebuild them.)
    set(c_icon_file ${CMAKE_SOURCE_DIR}/icons/${NAME}-icon.c)
  else()
    # Failing even that, include no-icon.c to satisfy the link-time
    # dependencies. The puzzles will build without nice icons.
    set(c_icon_file ${CMAKE_SOURCE_DIR}/no-icon.c)
  endif()

  set(${OUTVAR} ${c_icon_file} PARENT_SCOPE)
endfunction()

function(set_platform_puzzle_target_properties NAME TARGET)
  install(TARGETS ${TARGET})
endfunction()

function(build_platform_extras)
endfunction()
