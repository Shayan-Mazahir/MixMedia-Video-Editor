cmake_minimum_required(VERSION 3.25)
project(MixMedia VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo) # optimised, but still debuggable
endif()

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Multimedia Test)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus) # Linux desktop stuff, like "show in folder"
endif()
qt_standard_project_setup()

# Extra warnings, for compilers that understand GCC/Clang style flags
if(NOT MSVC)
    add_compile_options(-Wall -Wextra)
endif()

# engine/ = the video brains
# app/    = the window you actually click around in
# tools/  = little helpers for testing the engine from the terminal
# tests/  = automatic checks that pretend to be you clicking around
enable_testing()
add_subdirectory(engine)
add_subdirectory(app)
add_subdirectory(tools)
add_subdirectory(tests)
