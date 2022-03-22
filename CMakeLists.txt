cmake_minimum_required(VERSION 3.10)

project(shot VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)

set(CMAKE_CXX_FLAGS "-O3 -D_REENTRANT -Wall -Wextra -Wpedantic")

SET(CMAKE_EXPORT_COMPILE_COMMANDS true)

link_libraries(X11 Xext)

add_executable(shot
    src/main.cpp
)
