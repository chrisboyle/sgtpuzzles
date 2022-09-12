set(CMAKE_OSX_DEPLOYMENT_TARGET 10.6)
find_program(HALIBUT halibut REQUIRED)
set(CPACK_GENERATOR DragNDrop)
set(CPACK_PACKAGE_FILE_NAME Puzzles)
set(CPACK_DMG_VOLUME_NAME "Simon Tatham's Puzzle Collection")
include(CPack)
set(build_individual_puzzles FALSE)

set(CMAKE_OSX_ARCHITECTURES arm64 x86_64)

set(build_gui_programs FALSE) # they don't really fit in the OS X GUI model

function(get_platform_puzzle_extra_source_files OUTVAR NAME)
  set(${OUTVAR} PARENT_SCOPE)
endfunction()

function(set_platform_gui_target_properties TARGET)
endfunction()

function(set_platform_puzzle_target_properties NAME TARGET)
endfunction()

function(build_platform_extras)
  write_generated_games_header()

  set(resources
    ${CMAKE_CURRENT_SOURCE_DIR}/osx/Puzzles.icns)
  set_source_files_properties(${resources} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

  add_executable(puzzles MACOSX_BUNDLE
    osx.m list.c ${puzzle_sources}
    ${resources})

  set_target_properties(puzzles PROPERTIES
    OUTPUT_NAME Puzzles
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/osx/Info.plist)

  target_compile_definitions(puzzles PRIVATE COMBINED)
  target_include_directories(puzzles PRIVATE ${generated_include_dir})
  target_link_libraries(puzzles common ${platform_gui_libs} ${platform_libs}
    "-framework Cocoa")

  get_property(bundle_basename TARGET puzzles PROPERTY OUTPUT_NAME)
  set(help_dir ${CMAKE_CURRENT_BINARY_DIR}/${bundle_basename}.app/Contents/Resources/Help)
  message(${help_dir})
  add_custom_command(OUTPUT ${help_dir}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${help_dir}
    DEPENDS puzzles)
  add_custom_command(OUTPUT ${help_dir}/index.html
    COMMAND ${HALIBUT} --html
    ${CMAKE_CURRENT_SOURCE_DIR}/osx-help.but
    ${CMAKE_CURRENT_SOURCE_DIR}/puzzles.but
    DEPENDS
    ${help_dir}
    ${CMAKE_CURRENT_SOURCE_DIR}/osx-help.but
    ${CMAKE_CURRENT_SOURCE_DIR}/puzzles.but
    WORKING_DIRECTORY ${help_dir})
  add_custom_target(osx_help ALL
    DEPENDS ${help_dir}/index.html)

  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Puzzles.app
    USE_SOURCE_PERMISSIONS
    DESTINATION .)
endfunction()
