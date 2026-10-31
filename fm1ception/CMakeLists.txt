cmake_minimum_required(VERSION 3.22)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Link the MSVC runtime statically, so the plugin loads on a machine that has
# never had a Visual C++ redistributable installed.
if (POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(APPLE)
    # Universal by default, but overridable: a single-arch configure halves the
    # build while iterating, and CI wants to pin this explicitly.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures" FORCE)
    endif()
    set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    # An Xcode build signs with these when given ("Developer ID Application: Name (TEAMID)"),
    # else ad hoc. Release installers are signed by installer/macos/make_installer.sh.
    set(FM1C_CODESIGN_IDENTITY "" CACHE STRING "Xcode code signing identity (empty: ad hoc)")
    set(FM1C_DEVELOPMENT_TEAM  "" CACHE STRING "Xcode development team ID")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE        "Manual")
    if(FM1C_CODESIGN_IDENTITY)
        set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${FM1C_CODESIGN_IDENTITY}")
        set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM   "${FM1C_DEVELOPMENT_TEAM}")
        set(CMAKE_XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--timestamp --options runtime")
    else()
        set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")
    endif()
    set(CMAKE_XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES")
endif()

project(FM1Ception VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# JUCE — the shared checkout, as every other plugin here does
# ---------------------------------------------------------------------------
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Libraries/Juce"
    CACHE PATH "Path to JUCE framework")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${JUCE_DIR}\n"
        "Expected it as a sibling of this folder, at ../Libraries/Juce.\n"
        "Point at it explicitly with -DJUCE_DIR=/path/to/JUCE")
endif()

add_subdirectory(${JUCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

get_property(_juce_targets DIRECTORY ${JUCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _juce_targets)
    set_target_properties(${_t} PROPERTIES FOLDER "JUCE")
endforeach()

# ---------------------------------------------------------------------------
# The emulator — fm1-emu, a submodule, is NOT copied here.
#
# engine/ is a small Rust crate that compiles fm1-emu's CPU, SoC and its
# plug-in machine thread straight out of third_party/fm1-emu and exposes them
# as a C ABI. Cargo builds it once per architecture; a universal build joins
# them with lipo.
# ---------------------------------------------------------------------------
set(FM1EMU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fm1-emu")
if(NOT EXISTS "${FM1EMU_DIR}/crates/fm1-plugin/src/engine.rs")
    message(FATAL_ERROR
        "fm1-emu not found at ${FM1EMU_DIR}\n"
        "Run: git submodule update --init third_party/fm1-emu")
endif()

find_program(CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
set(ENGINE_CRATE "${CMAKE_CURRENT_SOURCE_DIR}/engine")
set(CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")

set(_cargo_env CARGO_TARGET_DIR=${CARGO_TARGET_DIR})
if(APPLE)
    # Every Apple Silicon Mac is at least an M1: tune the emulator for it.
    list(APPEND _cargo_env MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
         CARGO_TARGET_AARCH64_APPLE_DARWIN_RUSTFLAGS=-Ctarget-cpu=apple-m1)
    set(ENGINE_LIB "${CMAKE_BINARY_DIR}/libfm1ception_engine.a")
    set(_cargo_cmds)
    set(_arch_libs)
    foreach(_arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        if(_arch STREQUAL "arm64")
            set(_triple aarch64-apple-darwin)
        elseif(_arch STREQUAL "x86_64")
            set(_triple x86_64-apple-darwin)
        else()
            message(FATAL_ERROR "No Rust target for architecture ${_arch}")
        endif()
        list(APPEND _cargo_cmds COMMAND ${CMAKE_COMMAND} -E env ${_cargo_env}
             ${CARGO} build --release --manifest-path ${ENGINE_CRATE}/Cargo.toml --target ${_triple})
        list(APPEND _arch_libs "${CARGO_TARGET_DIR}/${_triple}/release/libfm1ception_engine.a")
    endforeach()
    # Only replace the joined library when it changed, so a no-op cargo run
    # does not relink the plugin.
    add_custom_target(fm1ception_engine_build
        ${_cargo_cmds}
        COMMAND lipo -create ${_arch_libs} -output ${ENGINE_LIB}.new
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ENGINE_LIB}.new ${ENGINE_LIB}
        BYPRODUCTS ${ENGINE_LIB}
        WORKING_DIRECTORY ${ENGINE_CRATE}
        COMMENT "Building the FM-1 engine (Rust): ${CMAKE_OSX_ARCHITECTURES}"
        USES_TERMINAL
        VERBATIM)
else()
    set(_cargo_target)
    if(MSVC)
        # match the plugin's static runtime
        list(APPEND _cargo_env "RUSTFLAGS=-Ctarget-feature=+crt-static")
        # the plugin's architecture, not the build machine's (an x64 build on ARM64 Windows)
        if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
            set(_triple aarch64-pc-windows-msvc)
        else()
            set(_triple x86_64-pc-windows-msvc)
        endif()
        set(_cargo_target --target ${_triple})
        set(ENGINE_LIB "${CARGO_TARGET_DIR}/${_triple}/release/fm1ception_engine.lib")
    else()
        set(ENGINE_LIB "${CARGO_TARGET_DIR}/release/libfm1ception_engine.a")
    endif()
    add_custom_target(fm1ception_engine_build
        COMMAND ${CMAKE_COMMAND} -E env ${_cargo_env}
                ${CARGO} build --release --manifest-path ${ENGINE_CRATE}/Cargo.toml ${_cargo_target}
        BYPRODUCTS ${ENGINE_LIB}
        WORKING_DIRECTORY ${ENGINE_CRATE}
        COMMENT "Building the FM-1 engine (Rust)"
        USES_TERMINAL
        VERBATIM)
endif()
set_target_properties(fm1ception_engine_build PROPERTIES FOLDER "Engine")

add_library(fm1ception_engine STATIC IMPORTED GLOBAL)
set_target_properties(fm1ception_engine PROPERTIES
    IMPORTED_LOCATION "${ENGINE_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${ENGINE_CRATE}/include")
add_dependencies(fm1ception_engine fm1ception_engine_build)
if(APPLE)
    set_property(TARGET fm1ception_engine APPEND PROPERTY INTERFACE_LINK_LIBRARIES iconv)
elseif(WIN32)
    set_property(TARGET fm1ception_engine APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ws2_32 userenv bcrypt ntdll advapi32 kernel32)
else()
    set_property(TARGET fm1ception_engine APPEND PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m)
endif()

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
set(FM1C_SOURCES
    Source/EngineHandle.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/EngineView.h
    Source/EngineView.cpp
    Source/FirmwarePrefs.h
    Source/CloudCatalog.h
    Source/CloudCatalog.cpp
    Source/CloudView.h
    Source/CloudView.cpp
    Source/FirmwareLibrary.h
    Source/FirmwareLibrary.cpp
    Source/CategoryPresets.h
    Source/CategoryPresets.cpp
    Source/InfoPanel.h
    Source/InfoPanel.cpp
    Source/ScreenCache.h
    Source/ScreenCache.cpp
    Source/LibraryWindow.h
    Source/LibraryWindow.cpp
    Source/OutputMeter.h
    Source/OutputMeter.cpp
    Source/Theme.h
)
source_group("Plugin" FILES ${FM1C_SOURCES})

if(APPLE)
    set(PLUGIN_FORMATS AU VST3 Standalone)
else()
    set(PLUGIN_FORMATS VST3 Standalone)
endif()

# Writing the VST3 manifest runs a helper JUCE builds for the target, which
# cannot execute on the build machine in a cross build. The manifest only
# caches metadata to speed host scanning; hosts load the plugin without it.
if(CMAKE_CROSSCOMPILING)
    set(FM1C_VST3_AUTO_MANIFEST FALSE)
else()
    set(FM1C_VST3_AUTO_MANIFEST TRUE)
endif()

juce_add_plugin(FM1Ception
    COMPANY_NAME             "Xfer Records"
    PLUGIN_MANUFACTURER_CODE Xfer
    PLUGIN_CODE              Fm1C
    FORMATS                  ${PLUGIN_FORMATS}
    PRODUCT_NAME             "FM-1Ception"
    BUNDLE_ID                "com.xferrecords.fm1ception"
    DESCRIPTION              "Up to eight emulated FM-1s"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    # installed by hand into /Library/Audio/Plug-Ins (the system folder), never the user's
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_AUTO_MANIFEST       ${FM1C_VST3_AUTO_MANIFEST}
    # The emulator compiles firmware to native code: under the hardened
    # runtime the Standalone needs MAP_JIT pages (a plugin gets its host's).
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.cs.allow-jit
)

juce_generate_juce_header(FM1Ception)

target_compile_features(FM1Ception PRIVATE cxx_std_20)
target_sources(FM1Ception PRIVATE ${FM1C_SOURCES})
target_include_directories(FM1Ception PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

set(FM1C_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    FM1C_EMU_VERSION="eb0d897"
)
target_compile_definitions(FM1Ception PUBLIC ${FM1C_DEFINITIONS} JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FM1Ception PRIVATE
    fm1ception_engine
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(FM1Ception PRIVATE /bigobj /MP /W3 /utf-8)
endif()

# ---------------------------------------------------------------------------
# Smoke test — renders through the real processor: engines come and go, a
# firmware (when one has been fetched) boots, plays and survives a state
# round trip. Headless, so it runs in CI.
# ---------------------------------------------------------------------------
option(FM1C_BUILD_TESTS "Build the console smoke test" ON)
if(FM1C_BUILD_TESTS)
    juce_add_console_app(FM1CeptionTests PRODUCT_NAME "FM1CeptionTests")
    juce_generate_juce_header(FM1CeptionTests)

    target_compile_features(FM1CeptionTests PRIVATE cxx_std_20)
    target_sources(FM1CeptionTests PRIVATE
        Tests/SmokeTest.cpp
        Source/PluginProcessor.cpp
        # createEditor() refers to it, so it has to be here even though the
        # test never opens a window.
        Source/PluginEditor.cpp
        Source/EngineView.cpp
        Source/CloudCatalog.cpp
        Source/CloudView.cpp
        Source/FirmwareLibrary.cpp
        Source/CategoryPresets.cpp
        Source/InfoPanel.cpp
        Source/ScreenCache.cpp
        Source/LibraryWindow.cpp
        Source/OutputMeter.cpp
    )
    target_include_directories(FM1CeptionTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_compile_definitions(FM1CeptionTests PUBLIC
        ${FM1C_DEFINITIONS}
        JUCE_STANDALONE_APPLICATION=1
        # The processor reports JucePlugin_Name, which only the plugin wrapper
        # defines. A console app has to supply it.
        JucePlugin_Name="FM-1Ception"
        JucePlugin_VersionString="${PROJECT_VERSION}"
    )
    target_link_libraries(FM1CeptionTests PRIVATE
        fm1ception_engine
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
    )

    enable_testing()
    add_test(NAME smoke COMMAND FM1CeptionTests)
endif()
