// SPDX-License-Identifier: GPL-2.0-or-later
//
// Airplane mode snapshot / restore planning - pure
// functions, unit-tested by test/qml/tst_airplane.qml. AirplaneMode.qml
// gathers the live state, asks these functions what to do, and applies the
// plan to WifiBackend and the Bluetooth interface.
//
// state = {
//   wifiEnabled: bool, wwanEnabled: bool, wwanAvailable: bool,
//   btAvailable: bool,            // BluetoothSection loaded and operational
//   btBlocked: bool,              // BluezQt.Manager.bluetoothBlocked
//   adapters: [{ address: "AA:BB:..", powered: bool }]
// }
// snapshot = { wifi: bool, wwan: bool, btBlocked: bool, adapters: ["AA:BB:..=1", ...] }
//   (stored in the applet config keys airplaneRestore*)
// plan = { wifi: bool, wwan: bool|null, btBlocked: bool|null, adapters: { "AA:BB:..": bool } }
//   null / missing = leave alone

.pragma library

function encodeAdapters(adapters) {
    var out = [];
    for (var i = 0; i < (adapters || []).length; ++i) {
        out.push(adapters[i].address + "=" + (adapters[i].powered ? "1" : "0"));
    }
    return out;
}

function decodeAdapters(list) {
    var map = {};
    for (var i = 0; i < (list || []).length; ++i) {
        var entry = String(list[i]);
        var eq = entry.lastIndexOf("=");
        if (eq <= 0) {
            continue;
        }
        map[entry.substring(0, eq)] = entry.substring(eq + 1) === "1";
    }
    return map;
}

/// What to remember before switching airplane mode on.
function snapshot(state) {
    return {
        wifi: !!state.wifiEnabled,
        wwan: !!state.wwanEnabled,
        btBlocked: !!state.btBlocked,
        adapters: state.btAvailable ? encodeAdapters(state.adapters) : []
    };
}

/// Switching on: every radio off.
function planOn(state) {
    var adapters = {};
    if (state.btAvailable) {
        for (var i = 0; i < (state.adapters || []).length; ++i) {
            adapters[state.adapters[i].address] = false;
        }
    }
    return {
        wifi: false,
        wwan: state.wwanAvailable ? false : null,
        btBlocked: state.btAvailable ? true : null,
        adapters: adapters
    };
}

/// Switching off: restore @p snap; without a snapshot, enable Wi-Fi and
/// Bluetooth. Adapters not in the snapshot follow the restored block state.
function planOff(snap, state) {
    var adapters = {};
    var hasSnap = !!snap;
    var btBlocked = hasSnap ? !!snap.btBlocked : false;
    if (state.btAvailable) {
        var saved = hasSnap ? decodeAdapters(snap.adapters) : {};
        for (var i = 0; i < (state.adapters || []).length; ++i) {
            var address = state.adapters[i].address;
            adapters[address] = saved.hasOwnProperty(address) ? saved[address] : !btBlocked;
        }
    }
    return {
        wifi: hasSnap ? !!snap.wifi : true,
        wwan: state.wwanAvailable ? (hasSnap ? !!snap.wwan : true) : null,
        btBlocked: state.btAvailable ? btBlocked : null,
        adapters: adapters
    };
}
