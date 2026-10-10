// SPDX-License-Identifier: GPL-2.0-or-later
//
// Card layout geometry - pure functions, unit-tested by test/qml/tst_layout.qml.
//
// Rules:
//   * the cards always fill the whole widget: it never scrolls and never
//     leaves an empty band;
//   * the cards keep their order and are arranged by nesting rows and
//     columns: columns of cards, where a place in a column can be a row of
//     cards side by side - or rows of cards, where a place in a row can be a
//     stack of cards. Light cards can so share a row next to or under a
//     heavy one;
//   * every card gets space in proportion to its content. Stacked cards
//     share the height by their content's height; cards side by side share
//     the width by the square root of their content, so a light one gets
//     narrower, not unusable - but first every card gets the width at which
//     its controls do not wrap, as far as the widget allows;
//   * a card's content is its controls (collapsed) plus its details, when
//     the user wants them inside the card. When a stack is too short for
//     everything, every card keeps its controls and what is left goes to
//     the details, the smallest first (the card whose details do not fit
//     whole shows as many rows of them as fit and keeps the arrow for the
//     rest);
//   * of all arrangements, the one that shows the most content wins, then
//     the one whose cards are filled the most and the most evenly. When
//     nothing fits (a tiny widget), the one that overflows least wins and
//     the cards clip;
//   * resolveExpanded() tells which card's flyout is open.

.pragma library

/// Index of the card whose details are open, or -1.
///
/// @p cards: [{ id, available, expandable }] of the SHOWN cards, in order
/// (expandable: the card has detail content). @p expandedId: the id the user
/// opened, "" for none. Returns -1 when that card is not shown, unavailable
/// or not expandable - the grid then forgets expandedId, so the flyout does
/// not reappear by itself when the card comes back.
function resolveExpanded(cards, expandedId) {
    if (!expandedId || !cards) {
        return -1;
    }
    for (var i = 0; i < cards.length; ++i) {
        var c = cards[i];
        if (c.id === expandedId) {
            return (c.available && c.expandable) ? i : -1;
        }
    }
    return -1;
}

// ---- arrangements ---------------------------------------------------------
//
// An arrangement is a tree: a leaf is a card's index; a node is
// { dir, kids, key, across } with dir "h" (kids side by side) or "v" (kids
// stacked). `across` is the largest number of cards side by side anywhere,
// `key` a string that identifies the arrangement.

function makeNode(dir, kids) {
    // A group of one is the thing itself; a row in a row is one row.
    var flat = [];
    for (var i = 0; i < kids.length; ++i) {
        var kid = kids[i];
        if (typeof kid !== "number" && kid.dir === dir) {
            for (var j = 0; j < kid.kids.length; ++j) {
                flat.push(kid.kids[j]);
            }
        } else {
            flat.push(kid);
        }
    }
    if (flat.length === 1) {
        return flat[0];
    }
    var keys = [];
    var across = dir === "h" ? 0 : 1;
    for (var k = 0; k < flat.length; ++k) {
        var leaf = typeof flat[k] === "number";
        keys.push(leaf ? String(flat[k]) : flat[k].key);
        var kidAcross = leaf ? 1 : flat[k].across;
        across = dir === "h" ? across + kidAcross : Math.max(across, kidAcross);
    }
    return { dir: dir, kids: flat, key: dir + "(" + keys.join(",") + ")", across: across };
}

var arrangementCache = {};

/// Every arrangement of @p count cards, three levels deep: between two
/// neighbouring cards the innermost group continues, a new middle group
/// starts, or a new outermost group does - once with columns outermost,
/// once with rows. Above 8 cards only columns of single cards (the count
/// triples per card otherwise), above 12 only even columns.
function arrangements(count) {
    if (count <= 0) {
        return [];
    }
    if (arrangementCache[count]) {
        return arrangementCache[count];
    }
    var out = [];
    var seen = {};
    function add(tree) {
        var key = typeof tree === "number" ? String(tree) : tree.key;
        if (!seen[key]) {
            seen[key] = true;
            out.push(tree);
        }
    }

    if (count > 12) {
        for (var columns = 1; columns <= count; ++columns) {
            var cols = [];
            var next = 0;
            for (var c = 0; c < columns; ++c) {
                var size = Math.floor((count + c) / columns);
                var stack = [];
                for (var s = 0; s < size; ++s) {
                    stack.push(next++);
                }
                cols.push(makeNode("v", stack));
            }
            add(makeNode("h", cols));
        }
        arrangementCache[count] = out;
        return out;
    }

    var deep = count <= 8;
    var choices = deep ? 3 : 2;
    var codes = Math.pow(choices, count - 1);
    var tops = deep ? ["h", "v"] : ["h"];
    for (var t = 0; t < tops.length; ++t) {
        var topDir = tops[t];
        var midDir = topDir === "h" ? "v" : "h";
        for (var code = 0; code < codes; ++code) {
            var top = [];
            var mid = [];
            var inner = [0];
            var rest = code;
            for (var gap = 0; gap < count - 1; ++gap) {
                // 0: same innermost group, 1: new middle group, 2: new outermost.
                var label = rest % choices;
                rest = Math.floor(rest / choices);
                if (!deep) {
                    ++label;
                }
                if (label >= 1) {
                    mid.push(makeNode(topDir, inner));
                    inner = [];
                }
                if (label === 2) {
                    top.push(makeNode(midDir, mid));
                    mid = [];
                }
                inner.push(gap + 1);
            }
            mid.push(makeNode(topDir, inner));
            top.push(makeNode(midDir, mid));
            add(makeNode(topDir, top));
        }
    }
    arrangementCache[count] = out;
    return out;
}

// ---- sizes -------------------------------------------------------------------

/// Shares @p total between items, @p spacing apart, in three steps: every
/// item its minimum (@p mins); then, as far as the total allows, up to the
/// width it is comfortable at (@p comforts, each at least its minimum; null:
/// the minimums); the rest in proportion to @p weights, nothing falling
/// back under its comfortable width.
function shares(weights, mins, total, spacing, comforts) {
    var n = weights.length;
    var available = Math.max(0, total - (n - 1) * spacing);
    var out = [];
    var i;
    var minSum = 0;
    var comfortSum = 0;
    var floors = [];
    for (i = 0; i < n; ++i) {
        floors.push(comforts ? Math.max(mins[i], comforts[i]) : mins[i]);
        minSum += mins[i];
        comfortSum += floors[i];
    }
    if (available <= minSum) {
        // Not even the minimums: in proportion to them.
        for (i = 0; i < n; ++i) {
            out.push(minSum > 0 ? available * mins[i] / minSum : available / n);
        }
        return out;
    }
    if (available <= comfortSum) {
        // Everybody the same part of the way from minimum to comfortable.
        var part = (available - minSum) / (comfortSum - minSum);
        for (i = 0; i < n; ++i) {
            out.push(mins[i] + (floors[i] - mins[i]) * part);
        }
        return out;
    }
    var pinned = [];
    for (i = 0; i < n; ++i) {
        pinned.push(false);
        out.push(0);
    }
    // Pin the items that would fall under their floor and share the rest
    // again; each round pins at least one, so this ends.
    for (var round = 0; round < n; ++round) {
        var free = available;
        var weight = 0;
        var unpinned = 0;
        for (i = 0; i < n; ++i) {
            if (pinned[i]) {
                free -= floors[i];
            } else {
                weight += weights[i];
                ++unpinned;
            }
        }
        var again = false;
        for (i = 0; i < n; ++i) {
            if (pinned[i]) {
                out[i] = floors[i];
            } else {
                out[i] = weight > 0 ? free * weights[i] / weight : free / unpinned;
                if (out[i] < floors[i]) {
                    pinned[i] = true;
                    again = true;
                }
            }
        }
        if (!again) {
            break;
        }
    }
    return out;
}

// What an arrangement needs, bottom-up: `min` is the height of the controls
// alone, `full` the height with all wanted details, `minWidth` the width
// under which a card gets too narrow, `natural` the width at which nothing
// in it has to wrap. Arrangements share most of their
// groups, so plans are kept per group (params.plans) for one compute().
function plan(node, params) {
    var id = typeof node === "number" ? node : node.key;
    var known = params.plans[id];
    if (known) {
        return known;
    }
    if (typeof node === "number") {
        var collapsed = Math.max(1, params.cards[node].collapsed || 0);
        var details = Math.max(0, params.cards[node].details || 0);
        known = {
            leaf: node, min: collapsed, full: collapsed + details, minWidth: params.minCardWidth,
            natural: Math.max(params.minCardWidth, params.cards[node].natural || 0)
        };
    } else {
        var kids = [];
        var gaps = (node.kids.length - 1) * params.spacing;
        var min = 0;
        var full = 0;
        var minWidth = 0;
        var natural = 0;
        for (var i = 0; i < node.kids.length; ++i) {
            var kid = plan(node.kids[i], params);
            kids.push(kid);
            if (node.dir === "v") {
                // Stacked: heights add up, the widest card sets the width.
                min += kid.min;
                full += kid.full;
                minWidth = Math.max(minWidth, kid.minWidth);
                natural = Math.max(natural, kid.natural);
            } else {
                // Side by side: one height for all, widths add up.
                min = Math.max(min, kid.min);
                full = Math.max(full, kid.full);
                minWidth += kid.minWidth;
                natural += kid.natural;
            }
        }
        if (node.dir === "v") {
            min += gaps;
            full += gaps;
        } else {
            minWidth += gaps;
            natural += gaps;
        }
        known = { dir: node.dir, key: node.key, kids: kids, gaps: gaps, min: min, full: full, minWidth: minWidth, natural: natural };
    }
    params.plans[id] = known;
    return known;
}

// Hands a rectangle (whole pixels) down the planned arrangement.
function place(p, x, y, width, height, params, out) {
    var spacing = params.spacing;
    var i;
    if (p.leaf !== undefined) {
        out.cards[p.leaf] = { x: x, y: y, width: width, height: height };
        return;
    }

    var last = p.kids.length - 1;
    if (p.dir === "v") {
        var available = Math.max(0, height - p.gaps);
        var mins = p.min - p.gaps;
        var fulls = p.full - p.gaps;
        // Controls first. What is left goes to the details, the smallest
        // first: short details are shown whole or not at all, long ones are
        // lists that can end after any row. Beyond that everything grows
        // alike. Too small for the controls alone: all squeezed alike.
        var parts = [];
        var total = 0;
        var order = [];
        for (i = 0; i <= last; ++i) {
            parts.push(p.kids[i].min);
            order.push(i);
        }
        if (available > mins) {
            order.sort(function (a, b) {
                return (p.kids[a].full - p.kids[a].min) - (p.kids[b].full - p.kids[b].min) || a - b;
            });
            var spare = fulls <= available ? Infinity : available - mins;
            for (i = 0; i <= last && spare > 0; ++i) {
                var extra = Math.min(spare, p.kids[order[i]].full - p.kids[order[i]].min);
                parts[order[i]] += extra;
                spare -= extra;
            }
        }
        for (i = 0; i <= last; ++i) {
            total += parts[i];
        }
        var top = y;
        for (i = 0; i <= last; ++i) {
            var bottom = i === last ? y + height : top + parts[i] * available / total;
            var kidY = Math.round(top);
            place(p.kids[i], x, kidY, width, Math.max(0, Math.round(bottom) - kidY), params, out);
            top = bottom + spacing;
        }
        return;
    }

    var widths = params.keep ? params.keep.widths[p.key] : null;
    if (!widths || widths.length !== p.kids.length) {
        var weights = [];
        var minWidths = [];
        var naturals = [];
        for (i = 0; i <= last; ++i) {
            var kid = p.kids[i];
            weights.push(Math.sqrt(Math.max(kid.min, Math.min(kid.full, height))));
            // Details need more width than controls alone.
            minWidths.push(kid.full > kid.min && height > kid.min ? Math.max(kid.minWidth, params.minInlineWidth)
                                                                  : kid.minWidth);
            naturals.push(kid.natural);
        }
        widths = shares(weights, minWidths, width, spacing, naturals);
    }
    out.widths[p.key] = widths;
    var left = x;
    for (i = 0; i <= last; ++i) {
        var right = i === last ? x + width : left + widths[i];
        var kidX = Math.round(left);
        place(p.kids[i], kidX, y, Math.max(0, Math.round(right) - kidX), height, params, out);
        left = right + spacing;
    }
}

/// Lays one arrangement out and scores it. Returns { key, cards, widths,
/// fits, shown, fill, uneven, score }; `widths` are the shares of every row,
/// by the row's key; `shown` is the share of all content that is on screen,
/// `fill` the share of the area that shows content and `uneven` how much
/// the cards differ in that. Every card also gets `details`: the height of
/// its details that fits inside it.
function evaluate(tree, params) {
    var p = plan(tree, params);
    var out = { key: p.key !== undefined ? p.key : String(tree), cards: [], widths: {}, fits: true, score: 0 };
    place(p, 0, 0, Math.round(params.width), Math.round(params.height), params, out);

    // Three things, in this order of weight: how much of all content
    // (controls and wanted details) is on screen, how much of the widget's
    // area shows content, and how evenly the cards are filled. Width beyond
    // what a card can use does not count as filled.
    var area = 0;
    var used = 0;
    var shownContent = 0;
    var allContent = 0;
    var overflow = 0;
    var narrow = 0;
    var wrapped = 0;
    var fills = [];
    var i;
    for (i = 0; i < out.cards.length; ++i) {
        var card = out.cards[i];
        var collapsed = Math.max(1, params.cards[i].collapsed || 0);
        var details = Math.max(0, params.cards[i].details || 0);
        // A card too narrow for its details keeps them behind the arrow.
        card.details = card.width + 0.5 >= params.minInlineWidth
            ? Math.max(0, Math.min(details, card.height - collapsed)) : 0;
        var content = Math.min(card.height, collapsed) + card.details;
        var cardArea = card.width * card.height;
        var cardUsed = Math.min(card.width, params.prefCardWidth) * content;
        area += cardArea;
        used += cardUsed;
        shownContent += content;
        allContent += collapsed + details;
        overflow += card.width * Math.max(0, collapsed - card.height);
        narrow += Math.max(0, params.minCardWidth - card.width) / params.minCardWidth;
        var natural = Math.max(params.minCardWidth, params.cards[i].natural || 0);
        wrapped += Math.max(0, natural - card.width) / natural;
        fills.push(cardArea > 0 ? cardUsed / cardArea : 0);
    }
    area = Math.max(1, area);
    var mean = used / area;
    var uneven = 0;
    for (i = 0; i < out.cards.length; ++i) {
        uneven += out.cards[i].width * out.cards[i].height / area * Math.abs(fills[i] - mean);
    }
    out.fits = overflow === 0 && narrow === 0;
    out.shown = shownContent / allContent;
    out.fill = mean;
    out.uneven = uneven;
    // Clipped or too narrow cards cost more than anything gained: such an
    // arrangement only wins when all of them are.
    // A card under its natural width wraps its chips and gets taller than
    // it was measured: a little worse than it looks.
    out.score = out.shown + 0.3 * mean - 0.3 * uneven - 0.3 * wrapped / out.cards.length
              - (out.fits ? 0 : 2 + 2 * overflow / area + 2 * narrow / out.cards.length);
    return out;
}

/// Lays out the cards so they fill the area, each in proportion to its
/// content.
///
/// params: {
///   width, height:  area to fill (px)
///   spacing:        gap between cards (px)
///   cards:          [{ collapsed, details, natural }] of the SHOWN cards,
///                   in order: the height of a card's controls, the extra
///                   height its details need inside the card (0: no
///                   details, or the user keeps them behind the arrow), and
///                   the width at which its controls do not wrap (0 or
///                   missing: any)
///   minCardWidth:   narrowest a card should get (px)
///   minInlineWidth: narrowest a card may be to show details inside (px;
///                   default minCardWidth). A narrower card keeps them
///                   behind the arrow however tall it is.
///   prefCardWidth:  the width a card can make use of (px); wider is fine
///                   but counts as empty space
///   current:        key of the arrangement on screen, or "". It is kept
///                   unless another one is clearly better, so a measurement
///                   that moves by a pixel does not reshuffle the cards.
///   keep:           { key, widths } of an earlier result, or null. Only
///                   that arrangement is laid out - with these widths when
///                   given (only the heights are worked out again), or with
///                   new ones.
/// }
///
/// Returns {
///   key, widths, fits,
///   cards: [{ x, y, width, height, details }]   (in card order, whole
///           pixels; details: the height of the card's details that fits)
/// }
function compute(params) {
    var count = params.cards ? params.cards.length : 0;
    if (count <= 0) {
        return { key: "", widths: {}, fits: true, cards: [] };
    }
    var p = {
        width: Math.max(0, params.width || 0),
        height: Math.max(0, params.height || 0),
        spacing: Math.max(0, params.spacing || 0),
        cards: params.cards,
        minCardWidth: Math.max(1, params.minCardWidth || 1),
        keep: null,
        plans: {}
    };
    p.minInlineWidth = Math.max(p.minCardWidth, params.minInlineWidth || 0);
    p.prefCardWidth = Math.max(p.minCardWidth, params.prefCardWidth || p.minCardWidth * 2);

    var trees = arrangements(count);
    var i;
    if (params.keep && params.keep.key) {
        for (i = 0; i < trees.length; ++i) {
            if ((typeof trees[i] === "number" ? String(trees[i]) : trees[i].key) === params.keep.key) {
                p.keep = params.keep.widths ? { widths: params.keep.widths } : null;
                return evaluate(trees[i], p);
            }
        }
    }

    // First the arrangements wide enough for their cards; when the widget
    // is too narrow for any, all of them.
    var best_ = null;
    for (var pass = 0; pass < 2 && best_ === null; ++pass) {
        for (i = 0; i < trees.length; ++i) {
            var across = typeof trees[i] === "number" ? 1 : trees[i].across;
            if (pass === 0 && across * p.minCardWidth + (across - 1) * p.spacing > p.width) {
                continue;
            }
            var candidate = evaluate(trees[i], p);
            if (candidate.key === params.current) {
                candidate.score += 0.05;
            }
            if (best_ === null || candidate.score > best_.score) {
                best_ = candidate;
            }
        }
    }
    return best_;
}
