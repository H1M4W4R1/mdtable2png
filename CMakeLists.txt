cmake_minimum_required(VERSION 3.16)
project(itable)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(itable main.cpp)

if(MSVC)
    target_compile_options(itable PRIVATE /W4)
    set_property(TARGET itable PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(itable PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    target_link_libraries(itable PRIVATE gdiplus)
endif()

if(MINGW)
    target_link_options(itable PRIVATE -static -static-libgcc -static-libstdc++)
endif()
