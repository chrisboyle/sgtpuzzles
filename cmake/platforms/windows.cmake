set(platform_common_sources windows.c printing.c)

set(platform_gui_libs
  user32.lib gdi32.lib comctl32.lib comdlg32.lib winspool.lib)

set(platform_libs)

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

function(get_platform_puzzle_extra_source_files OUTVAR NAME)
  set(${OUTVAR} ${CMAKE_SOURCE_DIR}/puzzles.rc PARENT_SCOPE)
endfunction()

function(set_platform_puzzle_target_properties NAME TARGET)
  if(DEFINED ICO_DIR AND EXISTS ${ICO_DIR}/${NAME}.ico)
    target_compile_definitions(${TARGET} PRIVATE ICON_FILE=\"${ICO_DIR}/${NAME}.ico\")
  endif()
  set_target_properties(${TARGET} PROPERTIES WIN32_EXECUTABLE ON)
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
