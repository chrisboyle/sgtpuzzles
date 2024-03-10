set(platform_common_sources emcc.c)
set(platform_gui_libs)
set(platform_libs)
set(CMAKE_EXECUTABLE_SUFFIX ".js")

set(WASM ON
  CACHE BOOL "Compile to WebAssembly rather than plain JavaScript")

# The minimal versions here are the ones that Puzzles' own JavaScript
# is written for. For most browsers, that's the earliest version whose
# WASM Emscripten is still willing to target (as of Emscripten
# 3.1.54). For Firefox _without_ WASM, we go back to Firefox 48
# because that's what KaiOS 2.5 is based on.
if(WASM)
  set(MIN_FIREFOX_VERSION 68 CACHE STRING
    "Oldest major version of Firefox to target")
else()
  set(MIN_FIREFOX_VERSION 48 CACHE STRING
    "Oldest major version of Firefox to target")
endif()
set(MIN_SAFARI_VERSION 150000 CACHE STRING
  "Oldest version of desktop Safari to target (XXYYZZ for version XX.YY.ZZ)")
set(MIN_CHROME_VERSION 57 CACHE STRING
  "Oldest version of Chrome to target")

find_program(HALIBUT halibut)
if(NOT HALIBUT)
  message(WARNING "HTML documentation cannot be built (did not find halibut)")
endif()

set(emcc_export_list
  # Event handlers for mouse and keyboard input
  _mouseup
  _mousedown
  _mousemove
  _key
  # Callback when the program activates timing
  _timer_callback
  # Callback from button presses in the UI outside the canvas
  _command
  # Text-formatting for copying to clipboard
  _get_text_format
  _free_text_format
  # Game-saving and game-loading functions
  _get_save_file
  _free_save_file
  _load_game
  # Callbacks to return values from dialog boxes
  _dlg_return_sval
  _dlg_return_ival
  # Callbacks when the resizing controls are used
  _resize_puzzle
  _restore_puzzle_size
  # Callback when device pixel ratio changes
  _rescale_puzzle
  # Callback for loading user preferences
  _prefs_load_callback
  # Functions for allocating and freeing C memory
  _malloc
  _free
  # Main program, run at initialisation time
  _main)

list(TRANSFORM emcc_export_list PREPEND \")
list(TRANSFORM emcc_export_list APPEND \")
string(JOIN "," emcc_export_string ${emcc_export_list})
set(CMAKE_C_LINK_FLAGS "\
-s ALLOW_MEMORY_GROWTH=1 \
-s ENVIRONMENT=web \
-s EXPORTED_FUNCTIONS='[${emcc_export_string}]' \
-s EXPORTED_RUNTIME_METHODS='[cwrap]' \
-s MIN_FIREFOX_VERSION=${MIN_FIREFOX_VERSION} \
-s MIN_SAFARI_VERSION=${MIN_SAFARI_VERSION} \
-s MIN_CHROME_VERSION=${MIN_CHROME_VERSION} \
-s MIN_NODE_VERSION=0x7FFFFFFF \
-s STRICT_JS=1")
if(WASM)
  set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -s WASM=1 -s WASM_BIGINT")
else()
  set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -s WASM=0")
endif()

set(build_cli_programs FALSE)
set(build_gui_programs FALSE)

function(get_platform_puzzle_extra_source_files OUTVAR NAME AUXILIARY)
  set(${OUTVAR} PARENT_SCOPE)
endfunction()

function(set_platform_gui_target_properties TARGET)
endfunction()

function(set_platform_puzzle_target_properties NAME TARGET)
  em_link_pre_js(${TARGET} ${CMAKE_SOURCE_DIR}/emccpre.js)
  em_link_js_library(${TARGET} ${CMAKE_SOURCE_DIR}/emcclib.js)
endfunction()

function(build_platform_extras)
  if(HALIBUT)
    set(help_dir ${CMAKE_CURRENT_BINARY_DIR}/help)
    add_custom_command(OUTPUT ${help_dir}/en
      COMMAND ${CMAKE_COMMAND} -E make_directory ${help_dir}/en)
    add_custom_command(OUTPUT ${help_dir}/en/index.html
      COMMAND ${HALIBUT} --html -Chtml-template-fragment:%k
        ${CMAKE_CURRENT_SOURCE_DIR}/puzzles.but
        ${CMAKE_CURRENT_SOURCE_DIR}/emcccopy.but
      DEPENDS
      ${help_dir}/en
      ${CMAKE_CURRENT_SOURCE_DIR}/puzzles.but
      ${CMAKE_CURRENT_SOURCE_DIR}/emcccopy.but
      WORKING_DIRECTORY ${help_dir}/en)
    add_custom_target(kaios_help ALL
      DEPENDS ${help_dir}/en/index.html)
  endif()

  # This is probably not the right way to set the destination.
  set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR} CACHE PATH
      "Installation path" FORCE)

  add_custom_target(kaios-extras ALL)

  foreach(name ${puzzle_names})
    add_custom_command(
      OUTPUT ${name}-manifest.webapp
      COMMAND ${PERL_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/kaios/manifest.pl
        "${name}" "${displayname_${name}}" "${description_${name}}"
        "${objective_${name}}" > "${name}-manifest.webapp"
      VERBATIM
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kaios/manifest.pl)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/kaios)
    add_custom_command(
      OUTPUT ${name}-kaios.html
      COMMAND ${PERL_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/kaios/apppage.pl
        "${name}" "${displayname_${name}}" > "${name}-kaios.html"
      VERBATIM
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kaios/apppage.pl)

    add_custom_target(${name}-kaios-extras
      DEPENDS ${name}-manifest.webapp ${name}-kaios.html)
    add_dependencies(kaios-extras ${name}-kaios-extras)

    install(TARGETS ${name} DESTINATION kaios/${name})
    # Release builds generate an initial memory image alongside the
    # JavaScript, but CMake doesn't seem to know about it to install
    # it.
    install(FILES $<TARGET_FILE:${name}>.mem OPTIONAL
      DESTINATION kaios/${name})
    install(FILES ${ICON_DIR}/${name}-56kai.png ${ICON_DIR}/${name}-112kai.png
      DESTINATION kaios/${name} OPTIONAL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${name}-kaios.html
      RENAME ${name}.html
      DESTINATION kaios/${name})
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${name}-manifest.webapp
      RENAME manifest.webapp
      DESTINATION kaios/${name})
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/kaios/kaiads-glue.js
      DESTINATION kaios/${name})
    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/help OPTIONAL
      DESTINATION kaios/${name})

  endforeach()
endfunction()
