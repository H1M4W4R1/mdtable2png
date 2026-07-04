cmake_minimum_required(VERSION 3.16)
project(mdtable2png)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mdtable2png main.cpp)

if(MSVC)
    target_compile_options(mdtable2png PRIVATE /W4)
    set_property(TARGET mdtable2png PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(mdtable2png PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    target_link_libraries(mdtable2png PRIVATE gdiplus)
endif()

if(MINGW)
    target_link_options(mdtable2png PRIVATE -static -static-libgcc -static-libstdc++)
endif()
