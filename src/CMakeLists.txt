# Target layout:
#   wlm2pst_common        static  portable utilities (+ *_win.cpp on Windows)
#   wlm2pst_worker_core   static  all worker logic except MAPI and main()
#   wlm2pst_launcher_core static  launcher detection logic (testable off-Windows)
#   wlm2pst_mapi          static  Windows-only Extended MAPI layer
#   wlm2pst_worker        exe     wlm2pst-worker-x86 / wlm2pst-worker-x64
#   wlm2pst_launcher      exe     wlm2pst (32-bit only)
#
# Source discovery uses GLOB CONFIGURE_DEPENDS so module directories stay
# self-contained. Windows-only translation units use the `_win.cpp` suffix.

configure_file(common/version/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/common/version/version.h @ONLY)

function(wlm2pst_collect_sources out_var)
    set(result "")
    foreach(pattern IN LISTS ARGN)
        file(GLOB_RECURSE matched CONFIGURE_DEPENDS ${pattern})
        list(APPEND result ${matched})
    endforeach()
    if(NOT WIN32)
        list(FILTER result EXCLUDE REGEX "_win\\.cpp$")
    endif()
    set(${out_var} ${result} PARENT_SCOPE)
endfunction()

# --- common ---
wlm2pst_collect_sources(WLM2PST_COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp)
add_library(wlm2pst_common STATIC ${WLM2PST_COMMON_SOURCES})
target_link_libraries(wlm2pst_common PUBLIC wlm2pst_settings)
target_include_directories(wlm2pst_common PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(WIN32)
    target_link_libraries(wlm2pst_common PUBLIC bcrypt)
endif()

# --- worker core (no MAPI, no main) ---
wlm2pst_collect_sources(WLM2PST_WORKER_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/worker/*.cpp)
list(FILTER WLM2PST_WORKER_CORE_SOURCES EXCLUDE REGEX "/worker/mapi/")
list(FILTER WLM2PST_WORKER_CORE_SOURCES EXCLUDE REGEX "/worker/main\\.cpp$")
if(WLM2PST_WORKER_CORE_SOURCES)
    add_library(wlm2pst_worker_core STATIC ${WLM2PST_WORKER_CORE_SOURCES})
    target_link_libraries(wlm2pst_worker_core PUBLIC wlm2pst_common wlm2pst_sqlite)
endif()

# --- launcher core ---
wlm2pst_collect_sources(WLM2PST_LAUNCHER_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/launcher/*.cpp)
list(FILTER WLM2PST_LAUNCHER_CORE_SOURCES EXCLUDE REGEX "/launcher/main\\.cpp$")
if(WLM2PST_LAUNCHER_CORE_SOURCES)
    add_library(wlm2pst_launcher_core STATIC ${WLM2PST_LAUNCHER_CORE_SOURCES})
    target_link_libraries(wlm2pst_launcher_core PUBLIC wlm2pst_common)
endif()

# --- Windows executables ---
if(WIN32)
    wlm2pst_collect_sources(WLM2PST_MAPI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/worker/mapi/*.cpp)
    if(WLM2PST_MAPI_SOURCES)
        add_library(wlm2pst_mapi STATIC ${WLM2PST_MAPI_SOURCES})
        target_link_libraries(wlm2pst_mapi PUBLIC wlm2pst_common ole32 oleaut32 advapi32)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/worker/main.cpp AND TARGET wlm2pst_mapi)
        add_executable(wlm2pst_worker worker/main.cpp)
        target_link_libraries(wlm2pst_worker PRIVATE wlm2pst_worker_core wlm2pst_mapi)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            wlm2pst_configure_windows_exe(wlm2pst_worker "wlm2pst-worker-x86"
                "WLM2PST conversion worker (x86)")
        else()
            wlm2pst_configure_windows_exe(wlm2pst_worker "wlm2pst-worker-x64"
                "WLM2PST conversion worker (x64)")
        endif()
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/launcher/main.cpp)
        add_executable(wlm2pst_launcher launcher/main.cpp)
        target_link_libraries(wlm2pst_launcher PRIVATE wlm2pst_launcher_core)
        wlm2pst_configure_windows_exe(wlm2pst_launcher "wlm2pst"
            "WLM2PST launcher - Windows Live Mail EML tree to Unicode PST")
    endif()
endif()
