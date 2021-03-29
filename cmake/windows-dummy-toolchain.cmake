# Fake CMake toolchain file, good enough to make cmake's initial
# configuration think it's going to build for Windows, but not good
# enough to actually do any building. The purpose is so that I can run
# Puzzles's CMakeLists.txt in Windows mode as far as making
# gamedesc.txt.

set(CMAKE_SYSTEM_NAME Windows)

set(CMAKE_C_COMPILER /bin/false)
set(CMAKE_LINKER /bin/false)
set(CMAKE_MT /bin/false)
set(CMAKE_RC_COMPILER /bin/false)

set(CMAKE_C_COMPILER_WORKS ON)
