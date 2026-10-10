// SPDX-License-Identifier: GPL-2.0-or-later
//
// Card.qml: details inside the card or behind the arrow.
//
// Whether details are inline once fed back into itself inside a single
// layout pass (details measured while hidden were shorter than when shown),
// which never terminated and froze plasmashell while the widget was being
// resized. These tests pin down what keeps that from coming back: details
// are measured at their real size, the collapsed height does not depend on
// the mode, and even self-contradicting content flips the mode a bounded
// number of times. They also cover what is shown: details the user wants in
// the card appear as far as whole rows fit, with the arrow for the rest.

import QtQuick
import QtQuick.Layouts
import QtTest

import "../../package/contents/ui"

Item {
    id: top

    width: 600
    height: 700

    // Fixed heights instead of real controls: the arithmetic stays obvious.
    Component {
        id: steadyCard

        Card {
            id: card
            property int flips: 0
            onDetailsInlineChanged: ++flips
            available: true
            wantInline: true
            title: "Steady"
            detailContent: Component {
                ColumnLayout {
                    Item { Layout.fillWidth: true; Layout.preferredHeight: 100 }
                }
            }
            Item { Layout.fillWidth: true; Layout.preferredHeight: 30 }
        }
    }

    // Three rows of 60 under a heading-less column, 4 apart.
    Component {
        id: rowsCard

        Card {
            id: card
            available: true
            wantInline: true
            title: "Rows"
            detailContent: Component {
                ColumnLayout {
                    spacing: 4
                    Item { Layout.fillWidth: true; Layout.preferredHeight: 60 }
                    Item { Layout.fillWidth: true; Layout.preferredHeight: 60 }
                    Item { Layout.fillWidth: true; Layout.preferredHeight: 60 }
                }
            }
            Item { Layout.fillWidth: true; Layout.preferredHeight: 30 }
        }
    }

    // Chips in a flow: 80 + 120 + 60 wide, 5 apart.
    Component {
        id: chipsCard

        Card {
            available: true
            title: "Chips"
            Flow {
                Layout.fillWidth: true
                spacing: 5
                Item { width: 80; height: 30 }
                Item { width: 120; height: 30 }
                Item { width: 60; height: 30; visible: false }
                Item { width: 60; height: 30 }
            }
        }
    }

    // Like Plasma's controls: smaller while not visible.
    Component {
        id: shyCard

        Card {
            id: card
            property int flips: 0
            onDetailsInlineChanged: ++flips
            available: true
            wantInline: true
            title: "Shy"
            detailContent: Component {
                ColumnLayout {
                    Item { Layout.fillWidth: true; Layout.preferredHeight: visible ? 160 : 20 }
                }
            }
            Item { Layout.fillWidth: true; Layout.preferredHeight: 30 }
        }
    }

    // Contradicts itself: too tall as soon as it is shown inline.
    Component {
        id: contraryCard

        Card {
            id: card
            property int flips: 0
            onDetailsInlineChanged: ++flips
            available: true
            wantInline: true
            title: "Contrary"
            detailContent: Component {
                ColumnLayout {
                    Item { Layout.fillWidth: true; Layout.preferredHeight: card.detailsInline ? 2000 : 20 }
                }
            }
            Item { Layout.fillWidth: true; Layout.preferredHeight: 30 }
        }
    }

    TestCase {
        name: "Card"
        when: windowShown

        function make(component, height) {
            const card = createTemporaryObject(component, top, { width: 300, height: height });
            verify(card);
            return card;
        }

        function test_inline_when_there_is_room() {
            const card = make(steadyCard, 400);
            tryCompare(card, "detailsInline", true);
            compare(card.expandable, false);
            compare(card.detailsShown, true);
        }

        function test_flyout_when_there_is_no_room() {
            const card = make(steadyCard, 150);
            wait(100);
            compare(card.detailsInline, false);
            compare(card.expandable, true);
            compare(card.flips, 0);
        }

        function test_whole_rows_and_the_arrow_for_the_rest() {
            const card = make(rowsCard, 600);
            tryCompare(card, "detailsInline", true);
            compare(card.detailsCut, 188);          // all three
            compare(card.detailsCutOff, false);
            compare(card.expandable, false);

            // Room for two rows and a bit: two rows, never half of the third.
            card.height = card.implicitHeight + card.detailsMargin + 150;
            tryCompare(card, "detailsCut", 124);
            compare(card.detailsInline, true);
            compare(card.detailsCutOff, true);
            compare(card.expandable, true);          // the arrow shows the rest

            // Less than one row: behind the arrow.
            card.height = card.implicitHeight + card.detailsMargin + 50;
            tryCompare(card, "detailsInline", false);
            compare(card.detailsCut, 0);
            compare(card.expandable, true);
        }

        function test_behind_the_arrow_by_default() {
            // chipsCard does not set wantInline; give it details and room.
            const card = make(chipsCard, 600);
            card.detailContent = Qt.createComponent("../../package/contents/ui/controls/StatusLine.qml");
            verify(card.detailContent.status === Component.Ready, card.detailContent.errorString());
            wait(200);
            compare(card.wantInline, false);
            compare(card.detailsInline, false);
            compare(card.expandable, true);
        }

        function test_behind_the_arrow_when_the_user_wants_that() {
            const card = make(steadyCard, 400);
            tryCompare(card, "detailsInline", true);
            card.wantInline = false;
            tryCompare(card, "detailsInline", false);
            compare(card.expandable, true);
            // The details are still measured: the flyout and the grid need them.
            verify(card.detailsExtent > 100);
            card.wantInline = true;
            tryCompare(card, "detailsInline", true);
        }

        function test_too_narrow_for_details() {
            const card = make(steadyCard, 400);
            tryCompare(card, "detailsInline", true);
            card.minInlineWidth = 350;               // the card is 300 wide
            tryCompare(card, "detailsInline", false);
            compare(card.expandable, true);
        }

        function test_natural_width_is_the_chips_in_one_row() {
            const card = make(chipsCard, 200);
            wait(50);
            // 80 + 5 + 120 + 5 + 60 and the padding on both sides; the hidden chip does not count.
            compare(card.naturalWidth, 270 + 2 * card.padding);
            // It does not depend on the card's own width (or the grid would chase it).
            card.width = 150;
            wait(50);
            compare(card.naturalWidth, 270 + 2 * card.padding);
            // No chips: no wish.
            compare(make(steadyCard, 200).naturalWidth, 0);
        }

        function test_cards_changing_together_all_follow() {
            // Several cards get their new size in the same pass when the
            // widget is resized; each one has to act on it. (Deferring with
            // Qt.callLater ran only one of them: the calls were taken for
            // the same function, and the other cards never showed their
            // details again.)
            const cards = [make(steadyCard, 150), make(steadyCard, 150), make(steadyCard, 150)];
            wait(100);
            cards.forEach(card => compare(card.detailsInline, false));
            cards.forEach(card => card.height = 400);
            cards.forEach(card => tryCompare(card, "detailsInline", true));
            cards.forEach(card => card.height = 150);
            cards.forEach(card => tryCompare(card, "detailsInline", false));
            cards.forEach(card => card.height = 400);
            cards.forEach(card => tryCompare(card, "detailsInline", true));
        }

        function test_follows_the_card_size() {
            // Again and again: the limit on flipping counts quick flips at
            // one size, not the changes of size. (It once counted for good,
            // and after two rounds the details never came back.)
            const card = make(steadyCard, 400);
            tryCompare(card, "detailsInline", true);
            for (let round = 0; round < 6; ++round) {
                card.height = 150;
                tryCompare(card, "detailsInline", false);
                card.height = 400 + round;
                tryCompare(card, "detailsInline", true);
            }
        }

        function test_follows_the_users_choice_again_and_again() {
            const card = make(steadyCard, 400);
            tryCompare(card, "detailsInline", true);
            for (let round = 0; round < 6; ++round) {
                card.wantInline = false;
                tryCompare(card, "detailsInline", false);
                card.wantInline = true;
                tryCompare(card, "detailsInline", true);
            }
        }

        function test_follows_content_that_comes_and_goes() {
            // Content can leave and return at one card size (Wi-Fi off and
            // on): after a quiet second the count starts again.
            const card = make(rowsCard, 600);
            tryCompare(card, "detailsInline", true);
            for (let round = 0; round < 3; ++round) {
                card.minInlineWidth = 1000;          // stands in for "no details now"
                tryCompare(card, "detailsInline", false);
                card.minInlineWidth = 0;
                tryCompare(card, "detailsInline", true, 3000);
            }
        }

        function test_collapsed_height_does_not_depend_on_the_mode() {
            const card = make(steadyCard, 150);
            wait(100);
            const collapsed = card.implicitHeight;
            card.height = 400;
            tryCompare(card, "detailsInline", true);
            wait(50);
            compare(card.implicitHeight, collapsed);
        }

        function test_details_are_measured_at_their_shown_size() {
            // Room for the hidden size (20) but not for the real one (160).
            const card = make(shyCard, 200);
            wait(200);
            compare(card.detailsInline, false);
            compare(card.flips, 0);
            card.height = 500;
            tryCompare(card, "detailsInline", true);
        }

        function test_contradicting_content_cannot_flip_for_ever() {
            const card = make(contraryCard, 300);
            // Would never return from the first frame if it looped inside
            // the layout pass.
            wait(500);
            // Two tries at inline, each taken back.
            verify(card.flips <= 4, "flipped " + card.flips + " times");
            const settled = card.flips;
            wait(300);
            compare(card.flips, settled);
            // Ends with the flyout, which always works.
            compare(card.detailsInline, false);
            compare(card.expandable, true);
        }
    }
}
