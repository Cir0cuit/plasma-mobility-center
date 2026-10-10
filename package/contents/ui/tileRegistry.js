// SPDX-License-Identifier: GPL-2.0-or-later
//
// The tiles the widget knows, and the pure functions that turn the stored
// configuration (tileOrder, hiddenTiles) into what is shown. No QML, no
// i18n() at runtime - titles are marked with I18N_NOOP for extraction and
// translated where they are displayed: i18n(entry.title).
// Unit-tested by test/qml/tst_order.qml.

.pragma library

function I18N_NOOP(text) {
    return text;
}

// id: stable config id (never rename - stored in users' configs)
// file: tiles/<file>
// title: untranslated title, i18n() it at the display site
// icon: freedesktop icon name
// kcm: the System Settings module the card's settings button opens
// details: the tile has details (shown in the card or behind its arrow)
var tiles = [
    { id: "brightness", file: "BrightnessTile.qml", title: I18N_NOOP("Brightness"), icon: "video-display-brightness", kcm: "kcm_powerdevilprofilesconfig", details: true },
    { id: "volume", file: "VolumeTile.qml", title: I18N_NOOP("Volume"), icon: "audio-volume-high", kcm: "kcm_pulseaudio", details: true },
    { id: "battery", file: "BatteryTile.qml", title: I18N_NOOP("Battery & Power"), icon: "battery-good", kcm: "kcm_powerdevilprofilesconfig", details: true },
    { id: "wireless", file: "WirelessTile.qml", title: I18N_NOOP("Wireless"), icon: "network-wireless", kcm: "kcm_networkmanagement", details: true },
    { id: "keyboardBacklight", file: "KeyboardBacklightTile.qml", title: I18N_NOOP("Keyboard Backlight"), icon: "input-keyboard-brightness", kcm: "kcm_powerdevilprofilesconfig", details: false },
    { id: "display", file: "DisplayTile.qml", title: I18N_NOOP("Display"), icon: "video-display", kcm: "kcm_kscreen", details: true },
    { id: "presentation", file: "PresentationTile.qml", title: I18N_NOOP("Presentation Mode"), icon: "x-office-presentation", kcm: "kcm_powerdevilprofilesconfig", details: false }
];

/// All known ids, in registry order.
function knownIds() {
    return tiles.map(function (t) { return t.id; });
}

/// The registry entry for @p id, or null.
function byId(id) {
    for (var i = 0; i < tiles.length; ++i) {
        if (tiles[i].id === id) {
            return tiles[i];
        }
    }
    return null;
}

function toArray(list) {
    // Plasmoid.configuration StringLists arrive as JS arrays or array-likes.
    var out = [];
    if (!list) {
        return out;
    }
    for (var i = 0; i < list.length; ++i) {
        out.push(String(list[i]));
    }
    return out;
}

/// The stored order merged with the known ids: known ids keep their stored
/// position, ids we do not know (any more) are dropped, duplicates are
/// dropped, and known ids missing from the stored order are appended in
/// registry order. Returns a NEW array.
function mergeOrder(stored, known) {
    var knownList = known === undefined ? knownIds() : toArray(known);
    var result = [];
    var storedList = toArray(stored);
    for (var i = 0; i < storedList.length; ++i) {
        var id = storedList[i];
        if (knownList.indexOf(id) !== -1 && result.indexOf(id) === -1) {
            result.push(id);
        }
    }
    for (var j = 0; j < knownList.length; ++j) {
        if (result.indexOf(knownList[j]) === -1) {
            result.push(knownList[j]);
        }
    }
    return result;
}

/// mergeOrder() minus the hidden ids. Returns a NEW array.
function visibleIds(stored, hidden, known) {
    var hiddenList = toArray(hidden);
    return mergeOrder(stored, known).filter(function (id) {
        return hiddenList.indexOf(id) === -1;
    });
}

/// @p order with @p id moved by @p delta places (clamped). Returns a NEW
/// array; unchanged copy if @p id is not in it.
function move(order, id, delta) {
    var result = toArray(order);
    var from = result.indexOf(id);
    if (from === -1) {
        return result;
    }
    var to = Math.max(0, Math.min(result.length - 1, from + delta));
    result.splice(from, 1);
    result.splice(to, 0, id);
    return result;
}

/// @p order with the element at @p from moved to @p to (drag and drop).
/// Returns a NEW array.
function moveIndex(order, from, to) {
    var result = toArray(order);
    if (from < 0 || from >= result.length || to < 0 || to >= result.length) {
        return result;
    }
    var id = result.splice(from, 1)[0];
    result.splice(to, 0, id);
    return result;
}

/// Whether the tile @p id has details at all.
function hasDetails(id) {
    var entry = byId(id);
    return entry !== null && entry.details === true;
}

/// @p hidden with @p id added (hide === true) or removed. Returns a NEW array.
/// Used for every list of tile ids kept as a set (hiddenTiles, inlineTiles).
function setHidden(hidden, id, hide) {
    var result = toArray(hidden).filter(function (h) { return h !== id; });
    if (hide) {
        result.push(id);
    }
    return result;
}
