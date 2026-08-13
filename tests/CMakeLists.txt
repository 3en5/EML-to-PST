# Unit tests run on native builds (Linux/macOS/Windows-MSVC). MinGW
# compile-check builds set WLM2PST_BUILD_TESTS=OFF.
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

file(GLOB WLM2PST_UNIT_TEST_SOURCES CONFIGURE_DEPENDS unit/*.cpp)
if(NOT WLM2PST_UNIT_TEST_SOURCES)
    return()
endif()

add_executable(wlm2pst_tests ${WLM2PST_UNIT_TEST_SOURCES})
target_link_libraries(wlm2pst_tests PRIVATE Catch2::Catch2WithMain wlm2pst_common)

if(MSVC)
    # C4324 ("structure was padded due to alignment specifier") is emitted from
    # Catch2's own headers (catch_optional.hpp), not from project code. Project
    # code keeps /W4 /WX; this only opts out of that single third-party warning,
    # consistent with wlm2pst_thirdparty_target().
    target_compile_options(wlm2pst_tests PRIVATE /wd4324)
endif()

# The scanner depth test builds a 48-level directory tree, which needs the same
# longPathAware manifest the shipped binaries carry (packaging/wlm2pst.manifest).
# Without it the test fails in its own setup on Windows rather than testing the
# scanner. Note this still additionally requires the machine-wide
# FileSystem\LongPathsEnabled=1 policy to take effect.
wlm2pst_configure_windows_exe(wlm2pst_tests "wlm2pst_tests" "WLM2PST unit tests")
foreach(lib wlm2pst_worker_core wlm2pst_launcher_core)
    if(TARGET ${lib})
        target_link_libraries(wlm2pst_tests PRIVATE ${lib})
    endif()
endforeach()

# Fixture EML files live in the repository; tests read them directly.
target_compile_definitions(wlm2pst_tests PRIVATE
    WLM2PST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

include(CTest)
include(Catch)
catch_discover_tests(wlm2pst_tests)

# Outlook-dependent integration tests (Windows-only; no-op elsewhere).
add_subdirectory(integration)
