#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Remove both halves, and the optional polkit rule if it is installed.
# Widget instances have to be removed from the desktop first, or plasmashell
# will show an empty placeholder until it is restarted.
set -uo pipefail

cd "$(dirname "$0")"

APPLET_ID="io.github.cir0cuit.mobilitycenter"
BASE_PATH="io/github/cir0cuit/mobilitycenter"
MODULES="core audio network display"
POLKIT_RULE_DST="/etc/polkit-1/rules.d/50-io.github.cir0cuit.mobilitycenter-chargelimit.rules"

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

# Where Qt keeps its QML modules. This is not the same path on every
# distribution - Fedora uses /usr/lib64/qt6/qml, Arch /usr/lib/qt6/qml, Debian
# and Ubuntu a multiarch directory - so ask rather than assume, in the same
# order of confidence CMakeLists.txt uses. Prints one candidate per line;
# duplicates and paths that do not exist are dropped by the caller.
qml_dir_candidates() {
    local d tool
    # What this build tree actually installed, if it is still around.
    if [ -f build/install_manifest.txt ]; then
        sed -n "s#\(.*\)/${BASE_PATH}/.*#\1#p" build/install_manifest.txt | head -1
    fi
    # Ask Qt itself, exactly as the build does.
    for tool in qtpaths6 qmake6; do
        if command -v "$tool" >/dev/null 2>&1; then
            d=$("$tool" -query QT_INSTALL_QML 2>/dev/null)
            [ -n "$d" ] && printf '%s\n' "$d"
        fi
    done
    # Last resort: the layouts the distributions actually use, for a machine
    # where Qt's development tools were never installed or have since gone.
    printf '%s\n' \
        /usr/lib64/qt6/qml \
        /usr/lib/qt6/qml \
        "/usr/lib/$(uname -m)-linux-gnu/qt6/qml"
}

echo "Removing the applet..."
if command -v kpackagetool6 >/dev/null 2>&1; then
    kpackagetool6 --type Plasma/Applet --remove "$APPLET_ID" || true
else
    echo "  kpackagetool6 not found - skipping (nothing to remove without it)."
fi

echo "Removing the QML modules..."
if [ -f build/install_manifest.txt ]; then
    as_root xargs -a build/install_manifest.txt rm -f
fi

# The manifest lists files, not directories, so the module directories
# survive the step above - and on a machine that installed from elsewhere
# there is no manifest at all. The module directories have to go first:
# rmdir leaves a non-empty directory alone, so one surviving module keeps
# every parent non-empty too, and the whole chain silently does nothing.
seen=""
found=0
while read -r qmldir; do
    [ -n "$qmldir" ] || continue
    # Resolve first: on Arch /usr/lib64 is a symlink to /usr/lib, so two
    # candidates can name the same directory.
    qmldir=$(readlink -f "$qmldir" 2>/dev/null) || continue
    [ -d "$qmldir/$BASE_PATH" ] || continue
    case ":$seen:" in *":$qmldir:"*) continue ;; esac
    seen="$seen:$qmldir"
    found=1
    for module in $MODULES; do
        if [ -d "$qmldir/$BASE_PATH/$module" ]; then
            echo "  $qmldir/$BASE_PATH/$module"
            as_root rm -rf "$qmldir/$BASE_PATH/$module"
        fi
    done
    # Parents only if empty: io/github/cir0cuit may also hold other widgets.
    as_root rmdir --ignore-fail-on-non-empty \
        "$qmldir/$BASE_PATH" \
        "$qmldir/io/github/cir0cuit" \
        "$qmldir/io/github" \
        "$qmldir/io" 2>/dev/null || true
done < <(qml_dir_candidates)

if [ "$found" -eq 0 ]; then
    echo "  no installed module found - nothing to remove."
fi

# The rules directory is not readable by users on every distribution
# (Fedora: drwxr-x--- root:polkitd), where a plain [ -e ] never sees the file.
# Ask as root in that case.
polkit_rule_present() {
    local dir
    dir=$(dirname "$POLKIT_RULE_DST")
    [ -d "$dir" ] || return 1
    if [ -r "$dir" ] && [ -x "$dir" ]; then
        [ -e "$POLKIT_RULE_DST" ]
    else
        as_root test -e "$POLKIT_RULE_DST"
    fi
}

if polkit_rule_present; then
    echo "Removing the optional polkit rule..."
    as_root rm -f "$POLKIT_RULE_DST"
fi

echo "Done. Restart plasmashell: systemctl --user restart plasma-plasmashell.service"
