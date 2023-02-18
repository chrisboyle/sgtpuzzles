set(PUZZLES_GTK_VERSION "ANY"
  CACHE STRING "Which major version of GTK to build with")
set_property(CACHE PUZZLES_GTK_VERSION
  PROPERTY STRINGS ANY 3 2)

set(STRICT OFF
  CACHE BOOL "Enable extra compiler warnings and make them errors")

set(NAME_PREFIX ""
  CACHE STRING "Prefix to prepend to puzzle binary names to avoid clashes \
in a crowded bin directory, e.g. \"sgt-\"")

find_package(PkgConfig REQUIRED)

find_program(HALIBUT halibut)
if(NOT HALIBUT)
  message(WARNING "HTML documentation cannot be built (did not find halibut)")
endif()

set(PUZZLES_GTK_FOUND FALSE)
macro(try_gtk_package VER PACKAGENAME)
  if(NOT PUZZLES_GTK_FOUND AND
      (PUZZLES_GTK_VERSION STREQUAL ANY OR
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
if(CMAKE_CROSSCOMPILING)
  # The puzzle icons are built by compiling and running a preliminary
  # set of puzzle binaries. We can't do that if the binaries won't run
  # on the build host.
  set(build_icons FALSE)
endif()

if(STRICT AND (CMAKE_C_COMPILER_ID MATCHES "GNU" OR
               CMAKE_C_COMPILER_ID MATCHES "Clang"))
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wwrite-strings -Wmissing-prototypes -std=c99 -pedantic -Werror")
endif()

if(STRICT AND (CMAKE_C_COMPILER_ID MATCHES "Clang"))
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-variable-declarations")
endif()

add_compile_definitions(HELP_DIR="${CMAKE_INSTALL_PREFIX}/share/sgt-puzzles/help")

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

function(set_platform_gui_target_properties TARGET)
endfunction()

function(set_platform_puzzle_target_properties NAME TARGET)
  get_target_property(official ${TARGET} official)
  get_target_property(exename ${TARGET} exename)
  get_target_property(displayname ${TARGET} displayname)
  get_target_property(description ${TARGET} description)
  set(binary_name ${NAME_PREFIX}${NAME})

  set_target_properties(${TARGET} PROPERTIES
    OUTPUT_NAME ${binary_name})

  if(${official})
    if(CMAKE_VERSION VERSION_LESS 3.14)
      # CMake 3.13 and earlier required an explicit install destination.
      install(TARGETS ${TARGET} RUNTIME DESTINATION bin)
    else()
      # 3.14 and above selects a sensible default, which we should avoid
      # overriding here so that end users can override it using
      # CMAKE_INSTALL_BINDIR.
      install(TARGETS ${TARGET})
    endif()
    configure_file(${CMAKE_SOURCE_DIR}/puzzle.desktop.in ${binary_name}.desktop)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/icons/${NAME}-48d24.png
      DESTINATION share/pixmaps OPTIONAL RENAME ${binary_name}-48d24.png)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${binary_name}.desktop
      DESTINATION share/applications)
  endif()
endfunction()

function(build_platform_extras)
  if(HALIBUT)
    set(help_dir ${CMAKE_CURRENT_BINARY_DIR}/help)
    add_custom_command(OUTPUT ${help_dir}/en
      COMMAND ${CMAKE_COMMAND} -E make_directory ${help_dir}/en)
    add_custom_command(OUTPUT ${help_dir}/en/index.html
      COMMAND ${HALIBUT} --html ${CMAKE_CURRENT_SOURCE_DIR}/puzzles.but
      DEPENDS
      ${help_dir}/en
      ${CMAKE_CURRENT_SOURCE_DIR}/puzzles.but
      WORKING_DIRECTORY ${help_dir}/en)
    add_custom_target(unix_help ALL
      DEPENDS ${help_dir}/en/index.html)

    install(DIRECTORY ${help_dir}
      DESTINATION share/sgt-puzzles)
  endif()
endfunction()
