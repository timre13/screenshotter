cmake_minimum_required(VERSION 3.10)

project(shot VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)

#set(CMAKE_CXX_FLAGS "-O3 -D_REENTRANT -Wall -Wextra -Wpedantic -pthread -fsanitize=address,leak,undefined")
set(CMAKE_CXX_FLAGS "-O3 -D_REENTRANT -Wall -Wextra -Wpedantic -pthread")

SET(CMAKE_EXPORT_COMPILE_COMMANDS true)

include_directories(
    /usr/include/gdk-pixbuf-2.0
    /usr/include/libpng16
    /usr/include/x86_64-linux-gnu
    /usr/include/libmount
    /usr/include/blkid
    /usr/include/glib-2.0
    /usr/lib/x86_64-linux-gnu/glib-2.0/include
)

link_libraries(X11 Xext GLX GL GLEW png16 z notify gdk_pixbuf-2.0 gio-2.0 gobject-2.0 glib-2.0)

add_executable(shot
    src/main.cpp
    src/Screenshot.cpp
)
