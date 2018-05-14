cmake_minimum_required(VERSION 3.4.1)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DANDROID -DSMALL_SCREEN -DSTYLUS_BASED -DNO_PRINTING -DCOMBINED -DVIVID_COLOURS")
file(GLOB puzzles_SRC "*.c")
add_library(puzzles SHARED ${puzzles_SRC})
# Only executables with library-ish filenames are included in the APK and unpacked on devices
add_executable(libpuzzlesgen.so "executable/android-gen.c")
target_link_libraries(libpuzzlesgen.so puzzles)