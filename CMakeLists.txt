cmake_minimum_required(VERSION 3.24)

# 1. BARE-METAL & CROSS-COMPILATION SAFEGUARDS
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

# 2. PROJECT SETUP
project(Glass C CXX ASM_NASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. FREESTANDING COMPILER OPTIONS
add_compile_options(
        -ffreestanding
        -O2
        -Wall
        -Wextra
        -fno-exceptions
        -fno-rtti
)

# 4. INCLUDE DIRECTORIES (Allows clean imports like #include "ui/MainWindow.h")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# 5. AUTOMATICALLY GATHER ALL SOURCE FILES
# This glob recursively grabs everything in your structured src/ directory
file(GLOB_RECURSE GLASS_SOURCES
        "src/*.cpp"
        "src/*.c"
        "src/*.asm"
        "src/*.h"
        "src/*.hpp"
)

# 6. DEFINE EXECUTABLE USING THE GATHERED SOURCES
add_executable(Glass ${GLASS_SOURCES})

# 7. OS-DEV LINKER OPTIONS
# Adjust 'linker.ld' path if you move it later, currently assuming root directory.
target_link_options(Glass PRIVATE
        -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
        -nostdlib
)