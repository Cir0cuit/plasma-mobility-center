// SPDX-License-Identifier: GPL-2.0-or-later
//
// tiles/airplane.js: snapshot and restore planning for airplane mode.

import QtQuick
import QtTest

import "../../package/contents/ui/tiles/airplane.js" as Airplane

TestCase {
    name: "Airplane"

    function liveState() {
        return {
            wifiEnabled: true, wwanEnabled: false, wwanAvailable: true,
            btAvailable: true, btBlocked: false,
            adapters: [{ address: "AA:BB:CC:DD:EE:01", powered: true }, { address: "AA:BB:CC:DD:EE:02", powered: false }]
        };
    }

    function test_adapterEncoding() {
        const encoded = Airplane.encodeAdapters(liveState().adapters);
        compare(encoded, ["AA:BB:CC:DD:EE:01=1", "AA:BB:CC:DD:EE:02=0"]);
        const decoded = Airplane.decodeAdapters(encoded);
        compare(decoded["AA:BB:CC:DD:EE:01"], true);
        compare(decoded["AA:BB:CC:DD:EE:02"], false);
        compare(Object.keys(Airplane.decodeAdapters(["garbage", "=1"])).length, 0);
    }

    function test_snapshot() {
        const snap = Airplane.snapshot(liveState());
        compare(snap.wifi, true);
        compare(snap.wwan, false);
        compare(snap.btBlocked, false);
        compare(snap.adapters.length, 2);
    }

    function test_planOn() {
        const plan = Airplane.planOn(liveState());
        compare(plan.wifi, false);
        compare(plan.wwan, false);
        compare(plan.btBlocked, true);
        compare(plan.adapters["AA:BB:CC:DD:EE:01"], false);
        compare(plan.adapters["AA:BB:CC:DD:EE:02"], false);
    }

    function test_planOnWithoutBluetoothOrModem() {
        const state = liveState();
        state.btAvailable = false;
        state.wwanAvailable = false;
        const plan = Airplane.planOn(state);
        compare(plan.wifi, false);
        compare(plan.wwan, null);
        compare(plan.btBlocked, null);
        compare(Object.keys(plan.adapters).length, 0);
    }

    function test_roundTrip() {
        const before = liveState();
        const snap = Airplane.snapshot(before);
        const during = { wifiEnabled: false, wwanEnabled: false, wwanAvailable: true, btAvailable: true, btBlocked: true,
                         adapters: [{ address: "AA:BB:CC:DD:EE:01", powered: false }, { address: "AA:BB:CC:DD:EE:02", powered: false }] };
        const plan = Airplane.planOff(snap, during);
        compare(plan.wifi, true);
        compare(plan.wwan, false);
        compare(plan.btBlocked, false);
        compare(plan.adapters["AA:BB:CC:DD:EE:01"], true);
        compare(plan.adapters["AA:BB:CC:DD:EE:02"], false);
    }

    function test_planOffWithoutSnapshot() {
        const during = liveState();
        const plan = Airplane.planOff(null, during);
        compare(plan.wifi, true);
        compare(plan.wwan, true);
        compare(plan.btBlocked, false);
        compare(plan.adapters["AA:BB:CC:DD:EE:01"], true);
        compare(plan.adapters["AA:BB:CC:DD:EE:02"], true);
    }

    function test_newAdapterFollowsRestoredBlock() {
        const snap = { wifi: true, wwan: false, btBlocked: true, adapters: [] };
        const during = liveState();
        const plan = Airplane.planOff(snap, during);
        compare(plan.btBlocked, true);
        compare(plan.adapters["AA:BB:CC:DD:EE:01"], false);
    }

    function test_snapshotWithoutBluetooth() {
        const state = liveState();
        state.btAvailable = false;
        const snap = Airplane.snapshot(state);
        compare(snap.adapters.length, 0);
        compare(snap.wifi, true);
    }

    function test_restoresRadiosThatWereOff() {
        // Wi-Fi was already off before airplane mode: it stays off afterwards.
        const before = liveState();
        before.wifiEnabled = false;
        before.wwanEnabled = true;
        const snap = Airplane.snapshot(before);
        const during = { wifiEnabled: false, wwanEnabled: false, wwanAvailable: true, btAvailable: true, btBlocked: true,
                         adapters: before.adapters };
        const plan = Airplane.planOff(snap, during);
        compare(plan.wifi, false);
        compare(plan.wwan, true);
    }

    function test_modemGoneLeavesWwanAlone() {
        const snap = Airplane.snapshot(liveState());
        const during = liveState();
        during.wwanAvailable = false;
        compare(Airplane.planOff(snap, during).wwan, null);
    }

    function test_configRoundTripOfSnapshot() {
        // The applet config stores the snapshot as plain keys; a StringList
        // comes back as a list-like object, not a JS array.
        const snap = Airplane.snapshot(liveState());
        const stored = { wifi: snap.wifi, wwan: snap.wwan, btBlocked: snap.btBlocked,
                         adapters: Array.from(snap.adapters.slice()) };
        const plan = Airplane.planOff(stored, liveState());
        compare(plan.adapters["AA:BB:CC:DD:EE:01"], true);
        compare(plan.adapters["AA:BB:CC:DD:EE:02"], false);
    }
}
