# SPDX-License-Identifier: GPL-2.0-or-later
#
# The four C++ QML modules the package imports. Each one lives on the QML
# import path of plasmashell itself, which is why this half is installed
# system-wide while the applet package is installed per user.
#
# Each module compiles every .h/.cpp in its directory (globbed with
# CONFIGURE_DEPENDS), so whoever implements a backend can add private helper
# files - named <backend>_*.{h,cpp}, e.g. core/screenbrightness_display.cpp -
# without touching this file.

# ---------------------------------------------------------------------------
# Dependencies. .core is mandatory (everything it needs ships wherever
# PowerDevil does); the other three are optional and only disable themselves.
# ---------------------------------------------------------------------------

find_package(KF6Solid ${MC_KF_MIN_VERSION} REQUIRED)
find_package(KF6Auth ${MC_KF_MIN_VERSION} REQUIRED)
find_package(KF6Config ${MC_KF_MIN_VERSION} REQUIRED)

find_package(KF6PulseAudioQt 1.6.0 QUIET)
find_package(KF6NetworkManagerQt ${MC_KF_MIN_VERSION} QUIET)
find_package(KF6Screen QUIET)

set(MC_BUILT_MODULES "core")

if(KF6PulseAudioQt_FOUND)
    set(MC_BUILD_AUDIO ON)
    list(APPEND MC_BUILT_MODULES "audio")
else()
    set(MC_BUILD_AUDIO OFF)
    message(WARNING "KF6PulseAudioQt >= 1.6.0 not found: skipping the .audio module (no Volume tile). "
                    "Install pulseaudio-qt-qt6-devel / libkf6pulseaudioqt-dev / pulseaudio-qt.")
endif()

if(KF6NetworkManagerQt_FOUND)
    set(MC_BUILD_NETWORK ON)
    list(APPEND MC_BUILT_MODULES "network")
else()
    set(MC_BUILD_NETWORK OFF)
    message(WARNING "KF6NetworkManagerQt not found: skipping the .network module (no Wi-Fi or airplane mode). "
                    "Install kf6-networkmanager-qt-devel / libkf6networkmanagerqt-dev / networkmanager-qt.")
endif()

if(KF6Screen_FOUND)
    set(MC_BUILD_DISPLAY ON)
    list(APPEND MC_BUILT_MODULES "display")
else()
    set(MC_BUILD_DISPLAY OFF)
    message(WARNING "KF6Screen (libkscreen) not found: skipping the .display module (no refresh-rate or projection controls). "
                    "Install libkscreen-devel / libkscreen-dev / libkscreen.")
endif()

set(MC_HAVE_CANBERRA OFF)
if(MC_BUILD_AUDIO AND MC_WITH_CANBERRA)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CANBERRA QUIET IMPORTED_TARGET libcanberra)
    endif()
    if(CANBERRA_FOUND)
        set(MC_HAVE_CANBERRA ON)
    else()
        message(STATUS "libcanberra not found: the Volume tile is built without its feedback sound "
                       "(install libcanberra-devel / libcanberra-dev / libcanberra, or pass -DMC_WITH_CANBERRA=OFF to silence this).")
    endif()
endif()

# Shared to the parent scope for test/CMakeLists.txt.
set(MC_BUILD_AUDIO ${MC_BUILD_AUDIO} PARENT_SCOPE)
set(MC_BUILD_NETWORK ${MC_BUILD_NETWORK} PARENT_SCOPE)
set(MC_BUILD_DISPLAY ${MC_BUILD_DISPLAY} PARENT_SCOPE)
set(MC_HAVE_CANBERRA ${MC_HAVE_CANBERRA} PARENT_SCOPE)

# Header-only helpers compiled into every module (see src/common/).
set(MC_COMMON_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/common/servicewatch.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/privatebus.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/dbusasync.h
)

# mc_add_qml_module(<suffix> SOURCES <files...> LIBS <targets...> [DEFINES <defs...>])
#   Builds URI ${MC_QML_URI_BASE}.<suffix> as plugin target mc_<suffix> into
#   build/qml/<uri path>, so that QML_IMPORT_PATH=build/qml works without
#   installing anything, and installs it into Qt's qml directory. Like the
#   terminal widget, the plugin is its own backing target (one MODULE .so).
#
#   With BUILD_TESTING it also builds mc_<suffix>_test, a static library of
#   the same sources (without QML registration) for the C++ unit tests.
#
# mc_module_sources(<var> <dir>) - every .h/.cpp directly in src/<dir>.
function(mc_module_sources var dir)
    file(GLOB found CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp")
    list(SORT found)
    set(${var} ${found} PARENT_SCOPE)
endfunction()

function(mc_add_qml_module suffix)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;DEFINES" ${ARGN})

    set(uri "${MC_QML_URI_BASE}.${suffix}")
    string(REPLACE "." "/" uri_path "${uri}")
    set(out_dir "${CMAKE_BINARY_DIR}/qml/${uri_path}")
    set(target mc_${suffix})

    set(common_defs
        MC_API_VERSION=${MC_API_VERSION}
        MC_VERSION_STRING="${PROJECT_VERSION}"
        MC_POLKIT_RULE_SOURCE="${MC_POLKIT_RULE_SOURCE}"
        MC_POLKIT_RULE_TARGET="${MC_POLKIT_RULE_TARGET}"
        ${ARG_DEFINES}
    )

    qt_add_qml_module(${target}
        URI ${uri}
        VERSION 1.0
        PLUGIN_TARGET ${target}
        OUTPUT_DIRECTORY ${out_dir}
        NO_PLUGIN_OPTIONAL
        SOURCES ${ARG_SOURCES} ${MC_COMMON_HEADERS}
    )
    # Every module compiles the header-only helpers of src/common (inline
    # functions, MC::DBus::SignalRelay's meta-object). Hidden visibility keeps
    # those symbols private to each plugin, so two modules loaded into the
    # same plasmashell never interpose each other's copies.
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/${suffix}
        ${CMAKE_CURRENT_SOURCE_DIR}/common
    )
    target_compile_definitions(${target} PRIVATE ${common_defs})
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Qml Qt6::DBus ${ARG_LIBS})

    install(DIRECTORY ${out_dir}/
        DESTINATION ${MC_QMLDIR}/${uri_path}
        USE_SOURCE_PERMISSIONS
        PATTERN "*.qrc" EXCLUDE
    )

    if(BUILD_TESTING)
        add_library(${target}_test STATIC ${ARG_SOURCES} ${MC_COMMON_HEADERS})
        target_include_directories(${target}_test PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/${suffix}
            ${CMAKE_CURRENT_SOURCE_DIR}/common
        )
        target_compile_definitions(${target}_test PUBLIC ${common_defs})
        target_compile_options(${target}_test PRIVATE -Wall -Wextra)
        target_link_libraries(${target}_test PUBLIC Qt6::Core Qt6::Qml Qt6::DBus ${ARG_LIBS})
    endif()

    message(STATUS "Building QML module ${uri}")
endfunction()

string(REPLACE ";" "," MC_BUILT_MODULES_CSV "${MC_BUILT_MODULES}")

# ---------------------------------------------------------------------------
# .core - PowerDevil, UPower, power-profiles-daemon, KWin Night Light,
#          fdo inhibitors. Mandatory: CenterView.qml imports it.
# ---------------------------------------------------------------------------
mc_module_sources(MC_CORE_SOURCES core)
mc_add_qml_module(core
    SOURCES ${MC_CORE_SOURCES}
    LIBS
        Qt6::DBus Qt6::Gui Qt6::Quick
        KF6::Solid KF6::AuthCore KF6::ConfigCore
    DEFINES
        MC_BUILT_MODULES="${MC_BUILT_MODULES_CSV}"
        $<$<BOOL:${MC_HAVE_CANBERRA}>:MC_BUILT_WITH_CANBERRA>
)

# ---------------------------------------------------------------------------
# .audio - PulseAudioQt (pulled in by plasma-pa), optional libcanberra.
# ---------------------------------------------------------------------------
if(MC_BUILD_AUDIO)
    mc_module_sources(MC_AUDIO_SOURCES audio)
    mc_add_qml_module(audio
        SOURCES ${MC_AUDIO_SOURCES}
        LIBS
            KF6::PulseAudioQt KF6::ConfigCore
            $<$<BOOL:${MC_HAVE_CANBERRA}>:PkgConfig::CANBERRA>
        DEFINES
            $<$<BOOL:${MC_HAVE_CANBERRA}>:HAVE_CANBERRA>
    )
endif()

# ---------------------------------------------------------------------------
# .network - NetworkManagerQt (pulled in by plasma-nm).
# ---------------------------------------------------------------------------
if(MC_BUILD_NETWORK)
    mc_module_sources(MC_NETWORK_SOURCES network)
    mc_add_qml_module(network
        SOURCES ${MC_NETWORK_SOURCES}
        LIBS
            KF6::NetworkManagerQt KF6::ConfigCore Qt6::DBus
    )
endif()

# ---------------------------------------------------------------------------
# .display - libkscreen (pulled in by kscreen).
# ---------------------------------------------------------------------------
if(MC_BUILD_DISPLAY)
    # Native output replication (real Wayland mirroring) needs libkscreen
    # >= 6.3.90 at build time; ProjectionLogic still checks
    # supportedFeatures() & PerOutputScaling at run time.
    set(MC_KSCREEN_REPLICATION OFF)
    if(KF6Screen_VERSION VERSION_GREATER_EQUAL 6.3.90)
        set(MC_KSCREEN_REPLICATION ON)
    endif()
    # Output::automaticBrightness() (ambient light sensor) arrived in
    # libkscreen 6.6; older builds simply never offer the switch.
    set(MC_KSCREEN_AUTOBRIGHTNESS OFF)
    if(KF6Screen_VERSION VERSION_GREATER_EQUAL 6.5.90)
        set(MC_KSCREEN_AUTOBRIGHTNESS ON)
    endif()
    mc_module_sources(MC_DISPLAY_SOURCES display)
    mc_add_qml_module(display
        SOURCES ${MC_DISPLAY_SOURCES}
        LIBS
            KF6::Screen
        DEFINES
            $<$<BOOL:${MC_KSCREEN_REPLICATION}>:MC_KSCREEN_REPLICATION>
            $<$<BOOL:${MC_KSCREEN_AUTOBRIGHTNESS}>:MC_KSCREEN_AUTOBRIGHTNESS>
    )
endif()

message(STATUS "Mobility Center modules: ${MC_BUILT_MODULES}; feedback sound: ${MC_HAVE_CANBERRA}")
