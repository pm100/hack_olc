include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_executable(hack_olc_tests test_engine.cpp)
target_compile_definitions(hack_olc_tests PRIVATE
    HACK_OLC_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(hack_olc_tests PRIVATE hack_engine Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(hack_olc_tests)
