cmake_minimum_required(VERSION 3.20)
project(chip8_emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)

add_executable(chip8 src/main.cpp src/Chip8.cpp)
target_include_directories(chip8 PRIVATE src ${SDL2_INCLUDE_DIRS})
target_link_libraries(chip8 PRIVATE ${SDL2_LIBRARIES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(chip8 PRIVATE -Wall -Wextra -Wpedantic)
endif()
