#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build (if needed) and install both halves of the widget: the four QML
# modules (system-wide, they must land on Qt's import path) and the applet
# package (per user, with kpackagetool6). Optionally installs a polkit rule
# for prompt-free charge-limit changes.
#
# Options:
#   --polkit-rule       also install the optional polkit rule (the widget
#                       offers it too, in the Battery & Power details)
#   --no-polkit-rule    do not install it (the default)
set -euo pipefail

cd "$(dirname "$0")"

APPLET_ID="io.github.cir0cuit.mobilitycenter"
POLKIT_RULE_SRC="data/polkit/50-io.github.cir0cuit.mobilitycenter-chargelimit.rules"
POLKIT_RULE_DST="/etc/polkit-1/rules.d/50-io.github.cir0cuit.mobilitycenter-chargelimit.rules"

polkit_choice="ask"
for arg in "$@"; do
    case "$arg" in
        --polkit-rule) polkit_choice="yes" ;;
        --no-polkit-rule) polkit_choice="no" ;;
        -h|--help)
            sed -n '3,10p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown option $arg (try --help)" >&2
            exit 1
            ;;
    esac
done

# Run a command as root. Already root: run it. Otherwise sudo, and if that is
# not installed (a minimal Debian or Arch may not have it), say so plainly
# rather than dying on "sudo: command not found".
as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "error: this step needs root and sudo is not installed." >&2
        echo "       Run the script as root, or: su -c '$*'" >&2
        exit 1
    fi
}

# Check the tools up front, so a missing package fails here with a useful
# message instead of somewhere in the middle of a CMake trace.
missing=()
command -v cmake >/dev/null 2>&1 || missing+=("cmake")
{ command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1; } || missing+=("a C++ compiler")
command -v kpackagetool6 >/dev/null 2>&1 || missing+=("kpackagetool6")
if [ ${#missing[@]} -gt 0 ]; then
    echo "error: missing ${missing[*]}." >&2
    echo "       See the Dependencies section of README.md for your distribution." >&2
    exit 1
fi

# The optional polkit rule (prompt-free charge-limit changes) is normally
# installed from the widget itself: Battery & Power -> details -> "Allow
# Changing the Limit". --polkit-rule installs it here instead.
if [ "$polkit_choice" = "ask" ]; then
    polkit_choice="no"
fi

if [ ! -d build ]; then
    cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF
fi
cmake --build build -j"$(nproc)"

# Optional modules are skipped by CMake when their library is missing; say
# which tiles that costs, so a missing -devel package is not a mystery.
for module in audio network display; do
    if [ ! -d "build/qml/io/github/cir0cuit/mobilitycenter/$module" ]; then
        echo "note: the .$module module was not built (its development package is missing);"
        echo "      the matching tiles will stay hidden. See README.md, Dependencies."
    fi
done

echo "Installing the QML modules (needs root: they must land on Qt's import path)..."
as_root cmake --install build

echo "Installing the applet for $USER..."
if kpackagetool6 --type Plasma/Applet --list 2>/dev/null | grep -q "^${APPLET_ID//./\\.}\$"; then
    kpackagetool6 --type Plasma/Applet --upgrade package
else
    kpackagetool6 --type Plasma/Applet --install package
fi

if [ "$polkit_choice" = "yes" ]; then
    if [ -d /etc/polkit-1/rules.d ] || [ -d /usr/share/polkit-1 ]; then
        as_root install -D -m 0644 "$POLKIT_RULE_SRC" "$POLKIT_RULE_DST"
        echo "Installed $POLKIT_RULE_DST (remove it, or run uninstall.sh, to undo)."
    else
        echo "warning: polkit does not seem to be installed - skipping the rule." >&2
    fi
fi

echo
echo "Done. Restart plasmashell to pick up the QML modules:"
echo "    systemctl --user restart plasma-plasmashell.service"
echo "Then add it from the desktop context menu: Add Widgets... -> Mobility Center"
