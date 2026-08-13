# WLM2PST - Windows Live Mail EML tree -> one Unicode PST.
#
# Build modes:
#   * MSVC (production): CMakePresets msvc-x86-* / msvc-x64-*.
#   * MinGW cross-compile (CI compile-check of all Windows code): mingw presets.
#   * Native Linux/macOS (unit tests of the portable core only): linux-tests preset.
cmake_minimum_required(VERSION 3.21)

project(WLM2PST
    VERSION 1.0.0
    DESCRIPTION "Convert a Windows Live Mail .eml folder tree into one Unicode PST"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WLM2PST_BUILD_TESTS "Build unit tests" ON)

if(WIN32)
    enable_language(RC)
endif()

include(cmake/wlm2pst_flags.cmake)
if(WIN32)
    include(cmake/wlm2pst_mapi_headers.cmake)
endif()

add_subdirectory(third_party/sqlite)
add_subdirectory(src)

if(WLM2PST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
