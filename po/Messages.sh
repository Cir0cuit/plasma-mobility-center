#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Extracts the translatable strings of the widget into
#   po/plasma_applet_io.github.cir0cuit.mobilitycenter.pot
#
# Works both standalone (sh po/Messages.sh) and under KDE's scripty, which
# sets $XGETTEXT and $podir itself. User-visible strings live in QML/JS only
#; tileRegistry.js marks titles with I18N_NOOP.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/.." && pwd)
podir=${podir:-$here}
domain="plasma_applet_io.github.cir0cuit.mobilitycenter"

if [ -z "${XGETTEXT:-}" ]; then
    XGETTEXT="xgettext --from-code=UTF-8 -C --kde -ci18n \
        -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3 \
        -kI18N_NOOP:1 -kI18NC_NOOP:1c,2 \
        --package-name=plasma-mobility-center --msgid-bugs-address=https://github.com/Cir0cuit/plasma-mobility-center/issues"
fi

cd "$src"
# shellcheck disable=SC2086 # XGETTEXT is a command line by contract
$XGETTEXT $(find package -name '*.qml' -o -name '*.js' | sort) -o "$podir/$domain.pot"
echo "Wrote $podir/$domain.pot"
