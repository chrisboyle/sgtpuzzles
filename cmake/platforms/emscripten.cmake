set(platform_common_sources emcc.c)
set(platform_gui_libs)
set(platform_libs)
set(CMAKE_EXECUTABLE_SUFFIX ".js")

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
  # Main program, run at initialisation time
  _main)

list(TRANSFORM emcc_export_list PREPEND \")
list(TRANSFORM emcc_export_list APPEND \")
string(JOIN "," emcc_export_string ${emcc_export_list})
set(CMAKE_C_LINK_FLAGS "\
-s ALLOW_MEMORY_GROWTH=1 \
-s EXPORTED_FUNCTIONS='[${emcc_export_string}]' \
-s EXTRA_EXPORTED_RUNTIME_METHODS='[cwrap,callMain]'")

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
  em_link_post_js(${TARGET} ${CMAKE_SOURCE_DIR}/emccpost.js)
endfunction()

function(build_platform_extras)
endfunction()
