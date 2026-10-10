// SPDX-License-Identifier: GPL-2.0-or-later
//
// layout.js: the cards always fill the widget, arranged in nested rows and
// columns and sized in proportion to their content; the flyout rule.

import QtQuick
import QtTest

import "../../package/contents/ui/layout.js" as Layout

TestCase {
    name: "Layout"

    // Seven cards like the real ones: controls of about the same height,
    // three of them with details.
    readonly property var seven: [
        { collapsed: 120, details: 0 },
        { collapsed: 90, details: 160 },
        { collapsed: 90, details: 190 },
        { collapsed: 90, details: 300 },
        { collapsed: 120, details: 0 },
        { collapsed: 100, details: 0 },
        { collapsed: 60, details: 0 }
    ]

    function base(extra) {
        return Object.assign({ spacing: 10, minCardWidth: 160, minInlineWidth: 210, prefCardWidth: 350, cards: seven }, extra);
    }

    function plain(count, collapsed) {
        const cards = [];
        for (let i = 0; i < count; ++i) {
            cards.push({ collapsed: collapsed, details: 0 });
        }
        return cards;
    }

    function keyOf(tree) {
        return typeof tree === "number" ? String(tree) : tree.key;
    }

    function test_empty() {
        const r = Layout.compute(base({ width: 400, height: 300, cards: [] }));
        compare(r.cards.length, 0);
        compare(r.key, "");
    }

    function test_arrangements() {
        compare(Layout.arrangements(0).length, 0);
        compare(Layout.arrangements(1).map(keyOf), ["0"]);
        compare(Layout.arrangements(2).map(keyOf).sort(), ["h(0,1)", "v(0,1)"]);
        const three = Layout.arrangements(3).map(keyOf);
        compare(three.length, 6);
        verify(three.indexOf("h(0,1,2)") >= 0);
        verify(three.indexOf("v(0,1,2)") >= 0);
        verify(three.indexOf("h(v(0,1),2)") >= 0);   // a stack next to a card
        verify(three.indexOf("v(0,h(1,2))") >= 0);   // a card over a row

        // Every arrangement holds every card once, in order, and no two are the same.
        const all = Layout.arrangements(7);
        const seen = {};
        for (const tree of all) {
            compare(tree.key.replace(/[^0-9]/g, ""), "0123456");
            verify(!seen[tree.key]);
            seen[tree.key] = true;
        }
        verify(all.length > 500);
        // Bounded for more cards than there are tiles.
        compare(Layout.arrangements(9).length, 256);
        compare(Layout.arrangements(20).length, 20);
    }

    function test_across() {
        const byKey = {};
        Layout.arrangements(4).forEach(tree => byKey[tree.key] = tree);
        compare(byKey["h(0,1,2,3)"].across, 4);
        compare(byKey["v(0,1,2,3)"].across, 1);
        compare(byKey["v(h(0,1),h(2,3))"].across, 2);
        compare(byKey["h(v(0,h(1,2)),3)"].across, 3);
    }

    function test_shares() {
        compare(Layout.shares([1, 4], [100, 100], 1010, 10), [200, 800]);
        // Nothing under its minimum: the light one is pinned, the rest shared again.
        compare(Layout.shares([1, 9, 9], [100, 100, 100], 1020, 10), [100, 450, 450]);
        compare(Layout.shares([1, 1], [300, 100], 510, 10), [300, 200]);
        // Not enough for the minimums: in proportion to them.
        compare(Layout.shares([1, 9], [100, 100], 110, 10), [50, 50]);
        compare(Layout.shares([1, 1], [300, 100], 210, 10), [150, 50]);
    }

    // The cards tile the area: whole pixels, inside it, no overlap, and
    // every edge either on the border or exactly `spacing` from a neighbour.
    function checkFills(r, width, height, spacing) {
        const cards = r.cards;
        function overlap(a0, a1, b0, b1) {
            return Math.min(a1, b1) - Math.max(a0, b0);
        }
        for (let i = 0; i < cards.length; ++i) {
            const c = cards[i];
            for (const v of [c.x, c.y, c.width, c.height]) {
                compare(v, Math.round(v));
            }
            verify(c.x >= 0 && c.y >= 0 && c.x + c.width <= width && c.y + c.height <= height, "card " + i + " inside");
            verify(c.width > 0 && c.height > 0, "card " + i + " has a size");
            for (let j = i + 1; j < cards.length; ++j) {
                const o = cards[j];
                verify(overlap(c.x, c.x + c.width, o.x, o.x + o.width) <= 0
                       || overlap(c.y, c.y + c.height, o.y, o.y + o.height) <= 0, "cards " + i + " and " + j + " overlap");
            }
            const beside = (edge, at) => cards.some(o => o !== c && at(o) === edge
                                                    && overlap(c.y, c.y + c.height, o.y, o.y + o.height) > 0);
            const stacked = (edge, at) => cards.some(o => o !== c && at(o) === edge
                                                     && overlap(c.x, c.x + c.width, o.x, o.x + o.width) > 0);
            verify(c.x === 0 || beside(c.x - spacing, o => o.x + o.width), "card " + i + " left");
            verify(c.x + c.width === width || beside(c.x + c.width + spacing, o => o.x), "card " + i + " right");
            verify(c.y === 0 || stacked(c.y - spacing, o => o.y + o.height), "card " + i + " top");
            verify(c.y + c.height === height || stacked(c.y + c.height + spacing, o => o.y), "card " + i + " bottom");
        }
    }

    function test_fillsArea_data() {
        return [
            { tag: "large", width: 1440, height: 816 },
            { tag: "medium", width: 1000, height: 600 },
            { tag: "odd", width: 601, height: 599 },
            { tag: "tall", width: 260, height: 900 },
            { tag: "narrow", width: 404, height: 684 },
            { tag: "strip", width: 1400, height: 260 },
            { tag: "tiny (does not fit)", width: 150, height: 100 },
        ];
    }

    function test_fillsArea(data) {
        const r = Layout.compute(base({ width: data.width, height: data.height }));
        compare(r.cards.length, seven.length);
        checkFills(r, data.width, data.height, 10);
    }

    function test_natural_width_comes_first() {
        // Minimums, then as far as possible the comfortable widths, then by weight.
        compare(Layout.shares([1, 1], [100, 100], 410, 10, [100, 250]), [150, 250]);
        compare(Layout.shares([1, 3], [100, 100], 810, 10, [300, 100]), [300, 500]);
        // Not enough for both comfortable widths: the same part of the way for each.
        compare(Layout.shares([1, 1], [100, 100], 360, 10, [200, 300]), [150, 200]);

        // Two equal cards side by side; the one whose chips need 300 gets them.
        const cards = [{ collapsed: 100, details: 0, natural: 300 }, { collapsed: 100, details: 0 }];
        const r = Layout.compute(base({ width: 510, height: 100, cards: cards }));
        compare(r.key, "h(0,1)");
        compare(r.cards.map(c => c.width), [300, 200]);
        // With room to spare both grow, none under its natural width.
        const wide = Layout.compute(base({ width: 810, height: 100, cards: cards }));
        compare(wide.cards.map(c => c.width), [400, 400]);
    }

    function test_height_follows_content() {
        // Too narrow for two side by side: one column, heights are the
        // contents times one factor.
        const cards = [{ collapsed: 100, details: 0 }, { collapsed: 200, details: 0 }, { collapsed: 300, details: 0 }];
        const r = Layout.compute(base({ width: 200, height: 1220, cards: cards }));
        compare(r.key, "v(0,1,2)");
        compare(r.cards.map(c => c.height), [200, 400, 600]);
        compare(r.cards.map(c => c.width), [200, 200, 200]);
    }

    function test_details_get_their_room() {
        const cards = [{ collapsed: 100, details: 0 }, { collapsed: 100, details: 200 }];
        // Room for both cards and the details: the second is three times the first.
        const roomy = Layout.compute(base({ width: 250, height: 410, cards: cards }));
        compare(roomy.cards.map(c => c.details), [0, 200]);
        compare(roomy.cards.map(c => c.height), [100, 300]);
        // Not enough for all of the details: the controls first, the details
        // get what is left.
        const tight = Layout.compute(base({ width: 250, height: 310, cards: cards }));
        compare(tight.cards.map(c => c.height), [100, 200]);
        compare(tight.cards.map(c => c.details), [0, 100]);
        // No details wanted in the card: the cards just share the height.
        const none = Layout.compute(base({ width: 250, height: 310, cards: plain(2, 100) }));
        compare(none.cards.map(c => c.height), [150, 150]);
    }

    function test_smallest_details_first() {
        // 250 to spare in the column: the short details whole, the long
        // ones get the rest.
        const cards = [{ collapsed: 50, details: 240 }, { collapsed: 50, details: 160 }];
        const r = Layout.compute(base({ width: 250, height: 360, cards: cards }));
        compare(r.cards.map(c => c.details), [90, 160]);
        compare(r.cards.map(c => c.height), [140, 210]);
        // 410 to spare: both whole.
        compare(Layout.compute(base({ width: 250, height: 520, cards: cards })).cards.map(c => c.details), [240, 160]);
    }

    function test_details_need_width() {
        const cards = [{ collapsed: 100, details: 200 }];
        compare(Layout.compute(base({ width: 180, height: 400, cards: cards })).cards[0].details, 0);
        compare(Layout.compute(base({ width: 210, height: 400, cards: cards })).cards[0].details, 200);
        // Side by side: the card with details gets the width for them.
        const pair = Layout.compute(base({ width: 400, height: 300, cards: [{ collapsed: 100, details: 200 }, { collapsed: 100, details: 0 }] }));
        compare(pair.key, "h(0,1)");
        compare(pair.cards[0].details, 200);
        verify(pair.cards[0].width >= 210);
        verify(pair.cards[1].width >= 160);
    }

    function test_cards_have_the_room_they_are_credited_with() {
        for (const size of [[1440, 816], [1000, 600], [700, 500], [420, 700], [1400, 260]]) {
            const r = Layout.compute(base({ width: size[0], height: size[1] }));
            verify(r.fits, String(size));
            r.cards.forEach((card, i) => {
                verify(card.height >= seven[i].collapsed, size + " card " + i);
                verify(card.width >= 160, size + " card " + i);
                verify(card.details <= seven[i].details, size + " card " + i);
                verify(card.details <= card.height - seven[i].collapsed, size + " card " + i);
                if (card.details > 0) {
                    verify(card.width >= 210, size + " card " + i);
                }
            });
        }
    }

    function test_shows_all_details_when_they_fit() {
        for (const size of [[1440, 816], [1000, 600]]) {
            const r = Layout.compute(base({ width: size[0], height: size[1] }));
            compare(r.cards.map(c => c.details), seven.map(c => c.details), String(size));
            compare(r.shown, 1);
        }
    }

    function test_shape_drives_the_arrangement() {
        compare(Layout.compute(base({ width: 260, height: 900 })).key, "v(0,1,2,3,4,5,6)");  // tall: one column
        // A strip: nothing stacked more than two high.
        const strip = Layout.compute(base({ width: 1400, height: 260 }));
        verify(strip.fits);
        verify(strip.cards.every(c => c.height > 260 / 3));
        // Large: not seven thin columns, although those show everything too.
        const large = Layout.compute(base({ width: 1440, height: 816 }));
        verify(large.cards.every(c => c.width >= 350));
    }

    function test_light_cards_share_a_row() {
        // One heavy card and three light ones, 700 x 520: stacked in one
        // column they would not leave room for the details (500 + 3 x 60 +
        // gaps > 520), in four columns the light ones would be mostly empty.
        const cards = [{ collapsed: 100, details: 300 }, { collapsed: 60, details: 0 }, { collapsed: 60, details: 0 }, { collapsed: 60, details: 0 }];
        const r = Layout.compute(base({ width: 700, height: 520, cards: cards }));
        compare(r.cards[0].details, 300);
        compare(r.key, "v(0,h(1,2,3))");
        compare(r.cards[1].y, r.cards[2].y);
        compare(r.cards[2].y, r.cards[3].y);
        verify(r.cards[0].height > 4 * r.cards[1].height);
    }

    function test_equal_cards_get_equal_space() {
        const r = Layout.compute(base({ width: 1010, height: 410, cards: plain(4, 100) }));
        const first = r.cards[0];
        for (const card of r.cards) {
            compare(card.width, first.width);
            compare(card.height, first.height);
        }
    }

    function test_squeezed_when_too_small() {
        const r = Layout.compute(base({ width: 150, height: 100 }));
        verify(!r.fits);
        verify(r.cards.every(c => c.details === 0));
        // Nothing clipped is accepted while something fits.
        verify(Layout.compute(base({ width: 700, height: 500 })).fits);
    }

    function test_keeps_the_current_arrangement_when_as_good() {
        // Four equal cards in a square: two rows of two and two columns of
        // two are the same picture. Whichever is on screen stays.
        const params = base({ width: 800, height: 800, cards: plain(4, 100) });
        for (const key of ["v(h(0,1),h(2,3))", "h(v(0,1),v(2,3))"]) {
            compare(Layout.compute(Object.assign({ current: key }, params)).key, key);
        }
        // A clearly better one still wins over the current one.
        compare(Layout.compute(Object.assign({ current: "h(v(0,1),v(2,3))" }, base({ width: 260, height: 900, cards: plain(4, 100) }))).key,
                "v(0,1,2,3)");
    }

    function test_keep() {
        const free = Layout.compute(base({ width: 1000, height: 600 }));
        // The arrangement alone: it stays, with new sizes.
        const stretched = Layout.compute(base({ width: 1200, height: 700, keep: { key: free.key } }));
        compare(stretched.key, free.key);
        checkFills(stretched, 1200, 700, 10);
        // With widths: they are used as they are, only heights follow content.
        const taller = seven.map(c => ({ collapsed: c.collapsed + 20, details: c.details }));
        const held = Layout.compute(base({ width: 1000, height: 600, cards: taller, keep: { key: free.key, widths: free.widths } }));
        compare(held.key, free.key);
        compare(held.cards.map(c => c.x), free.cards.map(c => c.x));
        compare(held.cards.map(c => c.width), free.cards.map(c => c.width));
        checkFills(held, 1000, 600, 10);
        // A stale one (another number of cards) is ignored.
        compare(Layout.compute(base({ width: 1000, height: 600, cards: plain(3, 100), keep: { key: free.key, widths: free.widths } })).cards.length, 3);
    }

    function test_resolveExpanded() {
        const cards = [
            { id: "a", available: true, expandable: true },
            { id: "b", available: true, expandable: false },
            { id: "c", available: false, expandable: true },
        ];
        compare(Layout.resolveExpanded(cards, ""), -1);
        compare(Layout.resolveExpanded(cards, "a"), 0);
        compare(Layout.resolveExpanded(cards, "b"), -1); // has no details
        compare(Layout.resolveExpanded(cards, "c"), -1); // unavailable
        compare(Layout.resolveExpanded(cards, "zzz"), -1); // not shown
        compare(Layout.resolveExpanded(null, "a"), -1);
    }
}
