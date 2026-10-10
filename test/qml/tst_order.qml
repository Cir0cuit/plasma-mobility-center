// SPDX-License-Identifier: GPL-2.0-or-later
//
// tileRegistry.js: merging the stored tile order with the known tiles, and
// the pure edit helpers ConfigTiles.qml uses.

import QtQuick
import QtTest

import "../../package/contents/ui/tileRegistry.js" as Registry

TestCase {
    name: "Order"

    readonly property var known: ["a", "b", "c", "d"]

    function test_registryIsConsistent() {
        const ids = Registry.knownIds();
        compare(ids.length, 7);
        for (const id of ids) {
            const entry = Registry.byId(id);
            verify(entry !== null, id);
            verify(entry.file.endsWith("Tile.qml"), id);
            verify(entry.title.length > 0, id);
        }
        compare(Registry.byId("nope"), null);
    }

    function test_defaultOrderMatchesConfigDefault() {
        // main.xml's tileOrder default lists every known id exactly once.
        const stored = ["brightness", "volume", "battery", "wireless", "keyboardBacklight", "display", "presentation"];
        compare(Registry.mergeOrder(stored), stored);
        compare(Registry.mergeOrder(stored).length, Registry.knownIds().length);
    }

    function test_mergeOrder_data() {
        return [
            { tag: "empty stored -> known order", stored: [], expected: ["a", "b", "c", "d"] },
            { tag: "undefined stored", stored: undefined, expected: ["a", "b", "c", "d"] },
            { tag: "stored order kept", stored: ["d", "c", "b", "a"], expected: ["d", "c", "b", "a"] },
            { tag: "new ids appended", stored: ["c", "a"], expected: ["c", "a", "b", "d"] },
            { tag: "unknown ids dropped", stored: ["x", "b", "y", "a"], expected: ["b", "a", "c", "d"] },
            { tag: "duplicates dropped", stored: ["b", "b", "a", "b"], expected: ["b", "a", "c", "d"] },
            { tag: "mixed", stored: ["old", "d", "d", "a", "gone"], expected: ["d", "a", "b", "c"] }
        ];
    }

    function test_mergeOrder(data) {
        compare(Registry.mergeOrder(data.stored, known), data.expected);
    }

    function test_mergeOrderReturnsNewArray() {
        const stored = ["a", "b", "c", "d"];
        const merged = Registry.mergeOrder(stored, known);
        verify(merged !== stored);
        merged.push("z");
        compare(stored.length, 4);
    }

    function test_visibleIds() {
        compare(Registry.visibleIds(["d", "a"], ["a", "c"], known), ["d", "b"]);
        compare(Registry.visibleIds([], [], known), known);
        compare(Registry.visibleIds([], ["unknown"], known), known);
    }

    function test_move() {
        compare(Registry.move(["a", "b", "c"], "b", -1), ["b", "a", "c"]);
        compare(Registry.move(["a", "b", "c"], "b", 1), ["a", "c", "b"]);
        compare(Registry.move(["a", "b", "c"], "a", -1), ["a", "b", "c"]);
        compare(Registry.move(["a", "b", "c"], "c", 5), ["a", "b", "c"]);
        compare(Registry.move(["a", "b", "c"], "x", 1), ["a", "b", "c"]);
        const original = ["a", "b"];
        const moved = Registry.move(original, "a", 1);
        compare(original, ["a", "b"]);
        verify(moved !== original);
    }

    function test_moveIndex() {
        compare(Registry.moveIndex(["a", "b", "c", "d"], 0, 2), ["b", "c", "a", "d"]);
        compare(Registry.moveIndex(["a", "b", "c", "d"], 3, 0), ["d", "a", "b", "c"]);
        compare(Registry.moveIndex(["a", "b"], 0, 9), ["a", "b"]);
    }

    function test_setHidden() {
        compare(Registry.setHidden([], "a", true), ["a"]);
        compare(Registry.setHidden(["a", "b"], "a", false), ["b"]);
        compare(Registry.setHidden(["a"], "a", true), ["a"]);
        const original = ["a"];
        verify(Registry.setHidden(original, "b", true) !== original);
        compare(original, ["a"]);
    }

    function test_hasDetails() {
        // Tiles with details offer "in the card / behind the arrow" in the settings.
        compare(Registry.knownIds().filter(id => Registry.hasDetails(id)), ["brightness", "volume", "battery", "wireless", "display"]);
        compare(Registry.hasDetails("nonsense"), false);
    }
}
