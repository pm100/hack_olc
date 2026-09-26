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
elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
    find_package(PNG REQUIRED)
    find_package(Threads REQUIRED)
    find_package(X11 REQUIRED)
    target_link_libraries(hack_engine PUBLIC OpenGL::GL PNG::PNG Threads::Threads X11::X11 X11::Xi)
endif()

if (EMSCRIPTEN)
    # -sUSE_LIBPNG must also be a *compile* flag, not just a link flag: it's
    # what makes Emscripten's ported libpng headers (png.h) available on the
    # include path for pge_impl.cpp, which olcPixelGameEngine.h pulls in.
    target_compile_options(hack_engine PUBLIC -sUSE_LIBPNG=1)
endif()

if (NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()

add_executable(hack_olc src/main.cpp)
target_link_libraries(hack_olc PRIVATE hack_engine)

if (EMSCRIPTEN)
    # WebGL/libpng flags for olcPixelGameEngine3's Host_Web_Emscripten backend,
    # plus preloading the bundled ROM into the virtual filesystem so the
    # no-argument fallback path in main.cpp finds it at "/tetris.hackem".
    target_link_options(hack_olc PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sMAX_WEBGL_VERSION=2
        -sMIN_WEBGL_VERSION=2
        -sUSE_LIBPNG=1
        # Default 64KB is too small; native builds get ~1MB+ from the OS.
        -sSTACK_SIZE=5MB
        # Host_Web_Emscripten calls emscripten_sleep() during startup, which
        # only works in an Asyncify-compiled build.
        -sASYNCIFY=1
        --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/../tetris.hackem@/tetris.hackem
    )
    set_target_properties(hack_olc PROPERTIES SUFFIX ".html")
else()
    add_custom_command(TARGET hack_olc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/../tetris.hackem
            $<TARGET_FILE_DIR:hack_olc>/tetris.hackem
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/../hello.hackem
            $<TARGET_FILE_DIR:hack_olc>/hello.hackem
    )
endif()
