// SPDX-License-Identifier: GPL-2.0-or-later
//
// ConfigTiles.qml (settings page) behaviour: every edit stores a NEW array
// in cfg_tileOrder / cfg_hiddenTiles, reordering keeps the list model in
// step (no reset while dragging), and the merged order follows
// tileRegistry.js (known ids in stored order + new ones - unknown ones).

import QtQuick
import QtTest

TestCase {
    id: testCase
    name: "ConfigTiles"
    when: windowShown

    width: 600
    height: 600

    readonly property var known: ["brightness", "volume", "battery", "wireless", "keyboardBacklight", "display", "presentation"]

    function createPage() {
        const component = Qt.createComponent(Qt.resolvedUrl("../../package/contents/ui/ConfigTiles.qml"), Component.PreferSynchronous);
        verify(component.status === Component.Ready, component.errorString());
        const page = createTemporaryObject(component, testCase, { width: 600, height: 600 });
        verify(page !== null);
        return page;
    }

    function modelIds(page) {
        return page.modelIds();
    }

    function test_unknownDroppedNewAppended() {
        const page = createPage();
        page.cfg_tileOrder = ["display", "gone", "volume"];
        compare(page.order, ["display", "volume", "brightness", "battery", "wireless", "keyboardBacklight", "presentation"]);
        compare(modelIds(page), page.order);
    }

    function test_detailsBehindArrowOrInCard() {
        const page = createPage();
        page.cfg_tileOrder = known.slice();
        compare(page.cfg_inlineTiles, []);
        compare(page.detailsInCard("wireless"), false);   // default: behind the arrow
        const before = page.cfg_inlineTiles;
        page.cfg_inlineTiles = page.setInCardFor("wireless", true);
        verify(page.cfg_inlineTiles !== before);
        compare(page.cfg_inlineTiles, ["wireless"]);
        compare(page.detailsInCard("wireless"), true);
        page.cfg_inlineTiles = page.setInCardFor("wireless", true);   // no duplicates
        compare(page.cfg_inlineTiles, ["wireless"]);
        page.cfg_inlineTiles = page.setInCardFor("volume", true);
        page.cfg_inlineTiles = page.setInCardFor("wireless", false);
        compare(page.cfg_inlineTiles, ["volume"]);

        // The choice reflects the setting and writes it. "Behind the arrow" comes first.
        const list = findChild(page, "tileList");
        verify(list !== null);
        list.forceLayout();
        const wireless = findChild(page, "detailsMode-wireless");
        const volume = findChild(page, "detailsMode-volume");
        const keyboard = findChild(page, "detailsMode-keyboardBacklight");
        verify(wireless !== null && volume !== null && keyboard !== null);
        compare(wireless.currentIndex, 0);
        compare(volume.currentIndex, 1);
        wireless.activated(1);
        compare(page.cfg_inlineTiles, ["volume", "wireless"]);
        volume.activated(0);
        compare(page.cfg_inlineTiles, ["wireless"]);
    }

    function test_saysThatDetailsNeedRoom() {
        const note = findChild(createPage(), "detailsNote");
        // (No text here: i18n() does not exist under qmltestrunner.)
        verify(note !== null);
    }

    function test_moveStoresNewArrayAndKeepsModel() {
        const page = createPage();
        page.cfg_tileOrder = known.slice();
        const before = page.cfg_tileOrder;
        page.moveTile(0, 2);
        verify(page.cfg_tileOrder !== before);
        compare(page.cfg_tileOrder, ["volume", "battery", "brightness", "wireless", "keyboardBacklight", "display", "presentation"]);
        compare(modelIds(page), page.cfg_tileOrder);
        // Out-of-range moves are ignored.
        page.moveTile(0, 99);
        compare(page.cfg_tileOrder[0], "volume");
    }

    function test_modelNotResetBySelfInflictedChange() {
        // A drag step must not rebuild the model (that destroys the dragged
        // delegate): the row object survives a move.
        const page = createPage();
        page.cfg_tileOrder = known.slice();
        const list = findChild(page, "tileList");
        verify(list !== null);
        list.forceLayout();
        const delegate = list.itemAtIndex(1);
        verify(delegate !== null);
        page.moveTile(1, 0);
        list.forceLayout();
        compare(list.itemAtIndex(0), delegate);
    }

    function test_hiddenTiles() {
        const page = createPage();
        page.cfg_hiddenTiles = [];
        verify(!page.isHidden("volume"));
        const before = page.cfg_hiddenTiles;
        page.cfg_hiddenTiles = page.setHiddenFor("volume", true);
        verify(page.cfg_hiddenTiles !== before);
        verify(page.isHidden("volume"));
        page.cfg_hiddenTiles = page.setHiddenFor("volume", false);
        verify(!page.isHidden("volume"));
    }
}
