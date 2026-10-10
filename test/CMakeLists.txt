# SPDX-License-Identifier: GPL-2.0-or-later
#
# Unit tests. Run with: ctest --test-dir build --output-on-failure
#
#   cpp/   QtTest. tst_api_* freeze the C++ <-> QML contract by checking the
#          static meta-objects (no instance, no D-Bus). Backend tests run
#          under dbus-run-session (a private session bus) and are expected
#          to use python-dbusmock for everything they talk to - see
#          mocks/README.md. They must never change live system state.
#   qml/   qmltestrunner tests of the pure JS libraries of the package.

find_package(Qt6 ${MC_QT_MIN_VERSION} REQUIRED COMPONENTS Test)

find_program(MC_DBUS_RUN_SESSION dbus-run-session)

# Safety net for every test: nothing a test does may reach the live session.
#   * D-Bus: backend tests (DBUS) get a fresh private session bus from
#     dbus-run-session; everything else gets an unusable session address. The
#     system bus is unusable by default - a test that needs one starts its own
#     with MockBus::SystemBus (cpp/mockbus.h), which overrides the variable.
#     An EMPTY address would mean "the default bus", i.e. the live one.
#   * KConfig files (kwinrc, plasma-nm, plasmaparc, ...) go to a scratch
#     home under the build tree, never to ~/.config.
#   * PulseAudio/PipeWire: an unreachable server, so a stray AudioBackend
#     cannot change the live volume.
#   * libkscreen: its in-process Fake backend, never KWin.
set(MC_TEST_HOME "${CMAKE_CURRENT_BINARY_DIR}/test-home")
file(MAKE_DIRECTORY "${MC_TEST_HOME}/config" "${MC_TEST_HOME}/data" "${MC_TEST_HOME}/cache" "${MC_TEST_HOME}/state")
set(MC_NO_BUS "unix:path=${CMAKE_CURRENT_BINARY_DIR}/no-such-bus-socket")
set(MC_TEST_ENV
    "QT_QPA_PLATFORM=offscreen"
    "MC_TEST_MOCKS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/mocks"
    "DBUS_SYSTEM_BUS_ADDRESS=${MC_NO_BUS}"
    "XDG_CONFIG_HOME=${MC_TEST_HOME}/config"
    "XDG_DATA_HOME=${MC_TEST_HOME}/data"
    "XDG_CACHE_HOME=${MC_TEST_HOME}/cache"
    "XDG_STATE_HOME=${MC_TEST_HOME}/state"
    "PULSE_SERVER=unix:${CMAKE_CURRENT_BINARY_DIR}/no-such-pulse-socket"
    "KSCREEN_BACKEND=Fake"
    "KSCREEN_BACKEND_INPROCESS=1"
)

# mc_add_cpp_test(<name> LIBS <targets...> [DBUS])
#   DBUS: run under dbus-run-session (private session bus). Without
#   dbus-run-session such a test is not registered at all - it must never
#   fall back to the live bus.
function(mc_add_cpp_test name)
    cmake_parse_arguments(ARG "DBUS" "" "LIBS" ${ARGN})
    if(ARG_DBUS AND NOT MC_DBUS_RUN_SESSION)
        message(WARNING "dbus-run-session not found - ${name} is not registered (it needs a private session bus)")
        return()
    endif()
    add_executable(${name} cpp/${name}.cpp cpp/apicheck.h cpp/mockbus.h)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cpp)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::DBus ${ARG_LIBS})
    set(env ${MC_TEST_ENV})
    if(ARG_DBUS)
        add_test(NAME ${name} COMMAND ${MC_DBUS_RUN_SESSION} -- $<TARGET_FILE:${name}>)
    else()
        add_test(NAME ${name} COMMAND ${name})
        list(APPEND env "DBUS_SESSION_BUS_ADDRESS=${MC_NO_BUS}")
    endif()
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}" TIMEOUT 60)
endfunction()

# The shared mock-bus helper checks itself (private buses, dbusmock).
mc_add_cpp_test(tst_mockbus DBUS)

# .core
mc_add_cpp_test(tst_api_core LIBS mc_core_test)
mc_add_cpp_test(tst_chargelimit_args LIBS mc_core_test)
mc_add_cpp_test(tst_screenbrightness LIBS mc_core_test DBUS)
mc_add_cpp_test(tst_keyboardbacklight LIBS mc_core_test DBUS)
mc_add_cpp_test(tst_batterymonitor LIBS mc_core_test DBUS)
mc_add_cpp_test(tst_powerprofiles LIBS mc_core_test DBUS)
mc_add_cpp_test(tst_chargelimit LIBS mc_core_test DBUS)
mc_add_cpp_test(tst_nightlight LIBS mc_core_test DBUS)
mc_add_cpp_test(tst_presentationmode LIBS mc_core_test DBUS)

if(MC_BUILD_AUDIO)
    mc_add_cpp_test(tst_api_audio LIBS mc_audio_test)
    mc_add_cpp_test(tst_devicefiltermodel LIBS mc_audio_test DBUS)
endif()

if(MC_BUILD_NETWORK)
    mc_add_cpp_test(tst_api_network LIBS mc_network_test)
    mc_add_cpp_test(tst_wificonnectionbuilder LIBS mc_network_test DBUS)
    mc_add_cpp_test(tst_wifibackend LIBS mc_network_test DBUS)
endif()

if(MC_BUILD_DISPLAY)
    mc_add_cpp_test(tst_api_display LIBS mc_display_test)
    mc_add_cpp_test(tst_projectionlogic LIBS mc_display_test DBUS)
    mc_add_cpp_test(tst_refreshratelogic LIBS mc_display_test DBUS)
    mc_add_cpp_test(tst_displayconfig LIBS mc_display_test DBUS)
endif()

# QML: the pure JS libraries of the package (layout.js, tileRegistry.js,
# tiles/airplane.js), the settings page (tst_configtiles), the card's
# inline-or-flyout decision (tst_card) and a compile
# check of every package file (tst_compile), run offscreen with qmltestrunner.
get_target_property(MC_QMAKE_FOR_TESTS Qt6::qmake IMPORTED_LOCATION)
execute_process(COMMAND ${MC_QMAKE_FOR_TESTS} -query QT_INSTALL_BINS
    OUTPUT_VARIABLE MC_QT_BINS OUTPUT_STRIP_TRAILING_WHITESPACE)
find_program(MC_QMLTESTRUNNER NAMES qmltestrunner qmltestrunner6 qmltestrunner-qt6
    HINTS ${MC_QT_BINS})
if(MC_QMLTESTRUNNER)
    foreach(qmltest tst_layout tst_order tst_configtiles tst_airplane tst_card tst_compile)
        add_test(NAME qml_${qmltest}
            COMMAND ${MC_QMLTESTRUNNER} -input ${CMAKE_CURRENT_SOURCE_DIR}/qml/${qmltest}.qml
                    -import ${CMAKE_BINARY_DIR}/qml)
        set_tests_properties(qml_${qmltest} PROPERTIES
            ENVIRONMENT "${MC_TEST_ENV};DBUS_SESSION_BUS_ADDRESS=${MC_NO_BUS}"
            TIMEOUT 60)
    endforeach()
else()
    message(WARNING "qmltestrunner not found - QML tests are not registered "
                    "(Fedora: qt6-qtdeclarative-devel, Debian: qt6-declarative-dev-tools, Arch: qt6-declarative)")
endif()
