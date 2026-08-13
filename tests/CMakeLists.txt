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
