# Vendored SQLite amalgamation (public domain). See README.md in this folder.
add_library(wlm2pst_sqlite STATIC sqlite3.c)

target_include_directories(wlm2pst_sqlite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(wlm2pst_sqlite PRIVATE
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0)

wlm2pst_thirdparty_target(wlm2pst_sqlite)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(wlm2pst_sqlite PUBLIC Threads::Threads)
endif()
