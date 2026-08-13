# Outlook-dependent integration tests (spec section 28): target
# `wlm2pst_integration_tests`. See integration_readme.md for how a
# maintainer wires this into a Windows build; this file supports BOTH of the
# two ways described there:
#
#   1. add_subdirectory(tests/integration) from the TOP-LEVEL CMakeLists.txt
#      (after add_subdirectory(src)) - the recommended, permanent wiring.
#      wlm2pst_mapi/_worker_core/_common already exist as targets in that
#      case, so this file reuses them directly.
#   2. `cmake -S tests/integration -B build/integration` standalone, with no
#      change to the top-level build at all. wlm2pst_mapi does not exist yet
#      in that case, so this file compiles the same source files itself
#      (deliberately NOT via add_subdirectory(../../) - the top-level
#      CMakeLists.txt and cmake/wlm2pst_flags.cmake key several include
#      paths off CMAKE_SOURCE_DIR, which point at THIS project's root, not
#      the repository root, when re-entered that way; every module here
#      instead names its own include directory explicitly).
#
# Windows-only: on any other host this whole directory is a documented no-op
# so configuring it never fails elsewhere, it just produces nothing to build.
cmake_minimum_required(VERSION 3.21)
# VERSION matches the top-level project() so version.h.in (shared with the
# main build) substitutes @PROJECT_VERSION...@ the same way in both modes.
project(wlm2pst_integration_tests_standalone VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(STATUS
        "tests/integration: mapi_integration_tests.cpp is Windows-only "
        "(requires classic Outlook + Extended MAPI); nothing to build here "
        "on this platform.")
    return()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WLM2PST_INTEGRATION_MAPI_LIB wlm2pst_mapi)
set(WLM2PST_INTEGRATION_WORKER_LIB wlm2pst_worker_core)
set(WLM2PST_INTEGRATION_COMMON_LIB wlm2pst_common)
set(WLM2PST_INTEGRATION_SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

if(NOT TARGET wlm2pst_mapi)
    # Mode 2 (standalone): build the handful of static libraries this
    # target needs from the real source tree, independently of the
    # top-level build (see the note above on why add_subdirectory(../../)
    # is deliberately avoided here).
    set(WLM2PST_INTEGRATION_MAPI_LIB wlm2pst_mapi_standalone)
    set(WLM2PST_INTEGRATION_WORKER_LIB wlm2pst_worker_core_standalone)
    set(WLM2PST_INTEGRATION_COMMON_LIB wlm2pst_common_standalone)
    set(WLM2PST_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

    configure_file(${WLM2PST_INTEGRATION_SRC_ROOT}/common/version/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/common/version/version.h @ONLY)

    if(NOT COMMAND wlm2pst_thirdparty_target)
        # third_party/sqlite/CMakeLists.txt calls this helper; it (and the
        # wlm2pst_settings interface target, unused here - every standalone
        # library below sets its own flags explicitly) live in the
        # top-level flags file.
        include(${WLM2PST_REPO_ROOT}/cmake/wlm2pst_flags.cmake)
    endif()
    if(NOT TARGET wlm2pst_sqlite)
        add_subdirectory(${WLM2PST_REPO_ROOT}/third_party/sqlite
            ${CMAKE_CURRENT_BINARY_DIR}/third_party/sqlite)
    endif()

    file(GLOB_RECURSE WLM2PST_INTEGRATION_COMMON_SOURCES CONFIGURE_DEPENDS
        ${WLM2PST_INTEGRATION_SRC_ROOT}/common/*.cpp)
    add_library(wlm2pst_common_standalone STATIC ${WLM2PST_INTEGRATION_COMMON_SOURCES})
    target_include_directories(wlm2pst_common_standalone PUBLIC
        ${WLM2PST_INTEGRATION_SRC_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_compile_definitions(wlm2pst_common_standalone PUBLIC
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN STRICT
        _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_compile_options(wlm2pst_common_standalone PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(wlm2pst_common_standalone PUBLIC bcrypt)

    file(GLOB_RECURSE WLM2PST_INTEGRATION_WORKER_SOURCES CONFIGURE_DEPENDS
        ${WLM2PST_INTEGRATION_SRC_ROOT}/worker/*.cpp)
    list(FILTER WLM2PST_INTEGRATION_WORKER_SOURCES EXCLUDE REGEX "/worker/mapi/")
    list(FILTER WLM2PST_INTEGRATION_WORKER_SOURCES EXCLUDE REGEX "/worker/main\\.cpp$")
    add_library(wlm2pst_worker_core_standalone STATIC ${WLM2PST_INTEGRATION_WORKER_SOURCES})
    target_include_directories(wlm2pst_worker_core_standalone PUBLIC ${WLM2PST_INTEGRATION_SRC_ROOT})
    target_compile_options(wlm2pst_worker_core_standalone PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(wlm2pst_worker_core_standalone PUBLIC
        wlm2pst_common_standalone wlm2pst_sqlite)

    file(GLOB_RECURSE WLM2PST_INTEGRATION_MAPI_SOURCES CONFIGURE_DEPENDS
        ${WLM2PST_INTEGRATION_SRC_ROOT}/worker/mapi/*.cpp)
    if(NOT WLM2PST_INTEGRATION_MAPI_SOURCES)
        message(WARNING
            "tests/integration: no worker/mapi/*.cpp sources found; "
            "skipping wlm2pst_integration_tests.")
        return()
    endif()
    add_library(wlm2pst_mapi_standalone STATIC ${WLM2PST_INTEGRATION_MAPI_SOURCES})
    target_include_directories(wlm2pst_mapi_standalone PUBLIC ${WLM2PST_INTEGRATION_SRC_ROOT})
    target_compile_options(wlm2pst_mapi_standalone PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(wlm2pst_mapi_standalone PUBLIC
        wlm2pst_common_standalone wlm2pst_worker_core_standalone
        ole32 oleaut32 advapi32 shlwapi)
endif()

find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(catch2)
endif()

add_executable(wlm2pst_integration_tests mapi_integration_tests.cpp)
target_include_directories(wlm2pst_integration_tests PRIVATE ${WLM2PST_INTEGRATION_SRC_ROOT})
target_link_libraries(wlm2pst_integration_tests PRIVATE
    Catch2::Catch2WithMain
    ${WLM2PST_INTEGRATION_MAPI_LIB}
    ${WLM2PST_INTEGRATION_WORKER_LIB}
    ${WLM2PST_INTEGRATION_COMMON_LIB})

# Fixture EMLs live in tests/fixtures/eml/, same convention as tests/unit.
target_compile_definitions(wlm2pst_integration_tests PRIVATE
    WLM2PST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures")

include(CTest)
include(Catch)
# [.outlook]-tagged cases are hidden (dot-tag) by default; CI can opt in with
# `wlm2pst_integration_tests.exe "[.outlook]"` on a Windows agent that has
# classic Outlook installed. catch_discover_tests still registers them so
# `ctest` with an explicit name/tag filter can select them.
catch_discover_tests(wlm2pst_integration_tests)
