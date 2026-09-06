cmake_minimum_required(VERSION 3.20)
project(hack_olc_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hack_engine STATIC
    src/pge_impl.cpp
    src/screen.cpp
    src/keyboard.cpp
)
target_include_directories(hack_engine PUBLIC src third_party)

if (WIN32)
    target_link_libraries(hack_engine PUBLIC user32 gdi32 opengl32 gdiplus Shlwapi dwmapi)
endif()

enable_testing()
add_subdirectory(tests)
