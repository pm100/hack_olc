cmake_minimum_required(VERSION 3.20)
project(hack_olc_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hack_engine STATIC
    src/pge_impl.cpp
    src/screen.cpp
    src/keyboard.cpp
    src/hack_engine.cpp
    src/code_loader.cpp
)
target_include_directories(hack_engine PUBLIC src third_party)

if (WIN32)
    target_link_libraries(hack_engine PUBLIC user32 gdi32 opengl32 gdiplus Shlwapi dwmapi)
endif()

enable_testing()
add_subdirectory(tests)

add_executable(hack_olc src/main.cpp)
target_link_libraries(hack_olc PRIVATE hack_engine)

add_custom_command(TARGET hack_olc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/../hello.hackem
        $<TARGET_FILE_DIR:hack_olc>/hello.hackem
)
