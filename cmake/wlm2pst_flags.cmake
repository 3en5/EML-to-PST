# Compiler/linker hardening and warnings-as-errors for WLM2PST project code.
# Third-party code (sqlite) opts out via wlm2pst_thirdparty_target().

# Interface target carrying project-wide compile settings.
add_library(wlm2pst_settings INTERFACE)

target_include_directories(wlm2pst_settings INTERFACE ${CMAKE_SOURCE_DIR}/src)

if(WIN32)
    target_compile_definitions(wlm2pst_settings INTERFACE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN STRICT
        _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

if(MSVC)
    target_compile_options(wlm2pst_settings INTERFACE
        /W4 /WX /permissive- /utf-8 /EHsc /sdl /guard:cf
        /Zc:__cplusplus /Zc:preprocessor /Zc:inline)
    add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_link_options(/LARGEADDRESSAWARE)
    else()
        add_link_options(/CETCOMPAT)
    endif()
else()
    target_compile_options(wlm2pst_settings INTERFACE -Wall -Wextra -Werror)
    if(MINGW)
        # wmain entry points and standalone binaries (no runtime DLLs to ship).
        target_compile_options(wlm2pst_settings INTERFACE -municode)
        add_link_options(-municode -static -static-libgcc -static-libstdc++)
    endif()
endif()

# Marks a third-party target: no warnings-as-errors, project defines only.
function(wlm2pst_thirdparty_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W0)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

# Configures a Windows executable target: output name, version resource,
# long-path/asInvoker manifest. `description` feeds the VERSIONINFO resource.
function(wlm2pst_configure_windows_exe target output_name description)
    if(NOT WIN32)
        return()
    endif()
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name})

    set(WLM2PST_RC_ORIGINAL_NAME "${output_name}.exe")
    set(WLM2PST_RC_DESCRIPTION "${description}")
    set(WLM2PST_RC_MANIFEST "${CMAKE_SOURCE_DIR}/packaging/wlm2pst.manifest")
    configure_file(${CMAKE_SOURCE_DIR}/cmake/version.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc @ONLY)
    target_sources(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc)

    if(MSVC)
        # The manifest is embedded through the .rc file; disable the linker's
        # default manifest to avoid a duplicate RT_MANIFEST resource.
        target_link_options(${target} PRIVATE /MANIFEST:NO)
    endif()
endfunction()
