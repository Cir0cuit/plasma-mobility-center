# SPDX-License-Identifier: GPL-2.0-or-later
#
# Mobility Center - laptop quick controls as a KDE Plasma 6 desktop widget.
#
# Two halves, built and installed like plasma-terminal-widget:
#   * package/   the KPackage (QML UI), installed per user with kpackagetool6;
#   * src/       four C++ QML modules that must land on Qt's own QML import
#                path, because a widget archive cannot install compiled code.
#
# The modules are split by the optional library each one needs, so a machine
# without (say) libkscreen loses only the display tile, not the whole widget.

cmake_minimum_required(VERSION 3.20)

project(plasma-mobility-center VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)

# Plasma 6.3's own floors (plasma-workspace v6.3.0): Qt 6.7, KF 6.10.
set(MC_QT_MIN_VERSION "6.7.0")
set(MC_KF_MIN_VERSION "6.10.0")

find_package(Qt6 ${MC_QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Qml Quick DBus)

# Modules use the :/qt/qml/ resource prefix (the Qt 6.5+ default).
qt_policy(SET QTP0001 NEW)

# The applet id and the QML module URIs. Keep them in sync with
# package/metadata.json and the imports in package/contents/ui/**.qml.
set(MC_APPLET_ID "io.github.cir0cuit.mobilitycenter")
set(MC_QML_URI_BASE "io.github.cir0cuit.mobilitycenter")

# Bumped whenever the C++ <-> QML contract changes incompatibly. The package
# compares it with ModuleInfo.apiVersion (see CenterView.qml), so a stale
# system-wide module next to a newer package shows a placeholder instead of
# half-working tiles.
set(MC_API_VERSION 2)

option(MC_WITH_CANBERRA "Play plasma-pa's volume feedback sound (needs libcanberra)" ON)
option(MC_INSTALL_PACKAGE_SYSTEMWIDE "Also install the applet package with cmake --install (install.sh uses kpackagetool6 per user instead)" OFF)
option(BUILD_TESTING "Build the unit tests" ON)

include(GNUInstallDirs)

# The optional polkit rule for prompt-free charge-limit changes. Installed
# root-owned with the modules, so the widget's "Allow" button can copy it
# into polkit's rules directory with pkexec without ever handing root a file
# the user could swap.
set(MC_POLKIT_RULE_NAME "50-${MC_APPLET_ID}-chargelimit.rules")
set(MC_POLKIT_RULE_SOURCE "${CMAKE_INSTALL_FULL_DATADIR}/${MC_APPLET_ID}/${MC_POLKIT_RULE_NAME}")
set(MC_POLKIT_RULE_TARGET "/etc/polkit-1/rules.d/${MC_POLKIT_RULE_NAME}")
install(FILES data/polkit/${MC_POLKIT_RULE_NAME} DESTINATION ${CMAKE_INSTALL_DATADIR}/${MC_APPLET_ID})

# The modules have to end up on plasmashell's QML import path, which is Qt's
# own qml directory - ask Qt where that is rather than guessing lib vs lib64.
if(NOT MC_QMLDIR)
    get_target_property(MC_QMAKE Qt6::qmake IMPORTED_LOCATION)
    execute_process(
        COMMAND ${MC_QMAKE} -query QT_INSTALL_QML
        OUTPUT_VARIABLE MC_QMLDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE MC_QMAKE_RESULT
    )
    if(NOT MC_QMAKE_RESULT EQUAL 0 OR MC_QMLDIR STREQUAL "")
        set(MC_QMLDIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/qt6/qml")
    endif()
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

# Translations: po/<lang>.po -> package/contents/locale/<lang>/LC_MESSAGES/*.mo.
# The .mo files are generated into the source package (gitignored), because
# kpackagetool6 installs that directory as-is.
file(GLOB MC_PO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/po/*.po")
if(MC_PO_FILES)
    find_program(MC_MSGFMT msgfmt)
    if(MC_MSGFMT)
        set(MC_MO_FILES)
        foreach(po ${MC_PO_FILES})
            get_filename_component(lang ${po} NAME_WE)
            set(mo "${CMAKE_CURRENT_SOURCE_DIR}/package/contents/locale/${lang}/LC_MESSAGES/plasma_applet_${MC_APPLET_ID}.mo")
            add_custom_command(OUTPUT ${mo}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/package/contents/locale/${lang}/LC_MESSAGES"
                COMMAND ${MC_MSGFMT} -o ${mo} ${po}
                DEPENDS ${po}
                COMMENT "Compiling ${lang} translation")
            list(APPEND MC_MO_FILES ${mo})
        endforeach()
        add_custom_target(mc_translations ALL DEPENDS ${MC_MO_FILES})
    else()
        message(WARNING "msgfmt (gettext) not found - translations are not compiled")
    endif()
endif()

if(MC_INSTALL_PACKAGE_SYSTEMWIDE)
    install(DIRECTORY package/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/${MC_APPLET_ID})
endif()

message(STATUS "QML modules '${MC_QML_URI_BASE}.*' install to ${MC_QMLDIR}")
