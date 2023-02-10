set(platform_common_sources emcc.c)
set(platform_gui_libs)
set(platform_libs)
set(CMAKE_EXECUTABLE_SUFFIX ".js")

set(WASM ON
  CACHE BOOL "Compile to WebAssembly rather than plain JavaScript")

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
  # Main program, run at initialisation time
  _main)

list(TRANSFORM emcc_export_list PREPEND \")
list(TRANSFORM emcc_export_list APPEND \")
string(JOIN "," emcc_export_string ${emcc_export_list})
set(CMAKE_C_LINK_FLAGS "\
-s ALLOW_MEMORY_GROWTH=1 \
-s EXPORTED_FUNCTIONS='[${emcc_export_string}]' \
-s EXTRA_EXPORTED_RUNTIME_METHODS='[cwrap]' \
-s STRICT_JS=1")
if(WASM)
  set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -s WASM=1")
else()
  set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -s WASM=0")
endif()

set(build_cli_programs FALSE)
set(build_gui_programs FALSE)

function(get_platform_puzzle_extra_source_files OUTVAR NAME)
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
      DEPENDS
      ${help_dir}/en
      ${CMAKE_CURRENT_SOURCE_DIR}/puzzles.but
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
