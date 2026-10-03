.pragma library

// A vim layer over the editor. Keys arrive as tokens ("x", "<Esc>", "<C-r>")
// and act through a small editor interface, so the logic runs the same on
// the real TextEdit and on a plain string in the tests:
//   text(), cursor(), setCursor(p), select(a, b), clearSelection(),
//   replace(a, b, text), undo(), redo(), pageLines(), optionally
//   scrollPage(fraction, from) and scrollLines(n), and host callbacks
//   save(), quit(force), openSearch(), openCommand(), message(text).

var NORMAL = "normal";
var INSERT = "insert";
var VISUAL = "visual";
var VISUAL_LINE = "visual-line";

var shiftWidth = "  ";

function create() {
    return {
        mode: NORMAL,
        count: "",
        operator: "",
        operatorCount: 1,
        prefix: "",
        register: { text: "", linewise: false },
        lastFind: null,
        lastSearch: null,
        anchor: 0,
        cursor: 0,
        column: -1,
        keys: [],
        lastChange: null,
        pendingInsertKeys: null,
        insertStart: 0,
        insertLength: 0,
        replaying: false
    };
}

// ---- text helpers -------------------------------------------------------

function lineStart(t, p) { return t.lastIndexOf("\n", p - 1) + 1; }
function lineEnd(t, p) { var e = t.indexOf("\n", p); return e < 0 ? t.length : e; }
function lastChar(t, p) { var s = lineStart(t, p), e = lineEnd(t, p); return e > s ? e - 1 : s; }

function firstNonBlank(t, p) {
    var s = lineStart(t, p), e = lineEnd(t, p);
    while (s < e && (t[s] === " " || t[s] === "\t"))
        ++s;
    return s;
}

function lineIndex(t, p) {
    var n = 0;
    for (var i = t.indexOf("\n"); i >= 0 && i < p; i = t.indexOf("\n", i + 1))
        ++n;
    return n;
}

function lineCount(t) { return t.split("\n").length; }

function startOfLine(t, index) {
    var pos = 0;
    for (var i = 0; i < index; ++i) {
        var next = t.indexOf("\n", pos);
        if (next < 0)
            return pos;
        pos = next + 1;
    }
    return pos;
}

function clampNormal(t, p) {
    p = Math.max(0, Math.min(t.length, p));
    return Math.min(p, lastChar(t, p));
}

function charClass(c, big) {
    if (c === undefined)
        return -1;
    if (/\s/.test(c))
        return 0;
    if (big)
        return 1;
    return /[\wÀ-￿]/.test(c) ? 2 : 1;
}

function wordForward(t, p, big) {
    var n = t.length;
    if (p >= n)
        return n;
    var c = charClass(t[p], big);
    if (c !== 0) {
        while (p < n && charClass(t[p], big) === c)
            ++p;
    }
    while (p < n && charClass(t[p], big) === 0) {
        if (t[p] === "\n" && t[p + 1] === "\n")
            return p + 1;
        ++p;
    }
    return p;
}

function wordEnd(t, p, big) {
    var n = t.length;
    ++p;
    while (p < n && charClass(t[p], big) === 0)
        ++p;
    if (p >= n)
        return Math.max(0, n - 1);
    var c = charClass(t[p], big);
    while (p + 1 < n && charClass(t[p + 1], big) === c)
        ++p;
    return p;
}

function wordBackward(t, p, big) {
    --p;
    while (p > 0 && charClass(t[p], big) === 0)
        --p;
    if (p <= 0)
        return 0;
    var c = charClass(t[p], big);
    while (p > 0 && charClass(t[p - 1], big) === c)
        --p;
    return p;
}

function paragraphForward(t, p) {
    var re = /\n[ \t]*\n/g;
    re.lastIndex = p;
    var m = re.exec(t);
    while (m && m.index + 1 <= p)
        m = re.exec(t);
    return m ? m.index + 1 : t.length;
}

function paragraphBackward(t, p) {
    var best = 0;
    var re = /\n[ \t]*\n/g;
    var m;
    while ((m = re.exec(t)) && m.index + 1 < p) {
        best = m.index + 1;
        re.lastIndex = m.index + 1;
    }
    return best;
}

function findInLine(t, p, ch, kind) {
    var s = lineStart(t, p), e = lineEnd(t, p), i;
    if (kind === "f" || kind === "t") {
        i = t.indexOf(ch, p + (kind === "t" ? 2 : 1));
        if (i < 0 || i >= e)
            return -1;
        return kind === "t" ? i - 1 : i;
    }
    i = t.lastIndexOf(ch, p - (kind === "T" ? 2 : 1));
    if (i < s)
        return -1;
    return kind === "T" ? i + 1 : i;
}

var pairs = { "(": ")", "[": "]", "{": "}", ")": "(", "]": "[", "}": "{" };

function matchPair(t, p) {
    var e = lineEnd(t, p);
    while (p < e && pairs[t[p]] === undefined)
        ++p;
    if (p >= e)
        return -1;
    var open = t[p], close = pairs[open];
    var forward = "([{".indexOf(open) >= 0;
    var depth = 0;
    for (var i = p; forward ? i < t.length : i >= 0; i += forward ? 1 : -1) {
        if (t[i] === open)
            ++depth;
        else if (t[i] === close && --depth === 0)
            return i;
    }
    return -1;
}

// Text objects return [start, end) or null.
function textObject(t, p, around, kind) {
    if (kind === "w" || kind === "W") {
        var big = kind === "W";
        var c = charClass(t[p], big);
        var s = p, e = p;
        while (s > lineStart(t, p) && charClass(t[s - 1], big) === c)
            --s;
        while (e < lineEnd(t, p) && charClass(t[e], big) === c)
            ++e;
        if (around) {
            var trailing = e;
            while (trailing < lineEnd(t, p) && charClass(t[trailing], big) === 0)
                ++trailing;
            if (trailing > e) {
                e = trailing;
            } else {
                while (s > lineStart(t, p) && charClass(t[s - 1], big) === 0)
                    --s;
            }
        }
        return [s, e];
    }
    if (kind === "\"" || kind === "'" || kind === "`") {
        var ls = lineStart(t, p), le = lineEnd(t, p);
        var before = t.lastIndexOf(kind, t[p] === kind ? p - 1 : p);
        var open = t[p] === kind && (before < ls) ? p : before;
        if (open < ls)
            return null;
        var close = t.indexOf(kind, open + 1);
        if (close < 0 || close >= le)
            return null;
        return around ? [open, close + 1] : [open + 1, close];
    }
    var brackets = { "(": "()", ")": "()", "b": "()", "[": "[]", "]": "[]",
                     "{": "{}", "}": "{}", "B": "{}", "<": "<>", ">": "<>" };
    if (brackets[kind]) {
        var o = brackets[kind][0], cl = brackets[kind][1];
        var depth = 0, start = -1;
        for (var i = p; i >= 0; --i) {
            if (t[i] === cl && i !== p)
                ++depth;
            else if (t[i] === o) {
                if (depth === 0) { start = i; break; }
                --depth;
            }
        }
        if (start < 0)
            return null;
        depth = 0;
        for (var j = start; j < t.length; ++j) {
            if (t[j] === o)
                ++depth;
            else if (t[j] === cl && --depth === 0)
                return around ? [start, j + 1] : [start + 1, j];
        }
        return null;
    }
    if (kind === "p") {
        var ps = paragraphBackward(t, p + 1), pe = paragraphForward(t, p);
        if (!around && pe > ps && t[pe - 1] === "\n")
            --pe;
        return [ps, pe];
    }
    return null;
}

// ---- motions ------------------------------------------------------------

// Returns { pos, linewise, inclusive } or null for keys that are not motions.
function motion(vim, ed, token, count) {
    var t = ed.text(), p = vim.mode === NORMAL ? ed.cursor() : vim.cursor;
    var i, target = p;
    var keepColumn = false;
    switch (token) {
    case "h": case "<Left>": case "<BS>":
        return { pos: Math.max(lineStart(t, p), p - count) };
    case "l": case "<Right>": case " ":
        return { pos: Math.min(vim.operator ? lineEnd(t, p) : lastChar(t, p), p + count) };
    case "0": case "<Home>":
        return { pos: lineStart(t, p) };
    case "^":
        return { pos: firstNonBlank(t, p) };
    case "$": case "<End>":
        target = p;
        for (i = 1; i < count; ++i)
            target = lineEnd(t, target) + 1;
        return { pos: lastChar(t, Math.min(target, t.length)), inclusive: true };
    case "w": case "W":
        for (i = 0; i < count; ++i)
            target = wordForward(t, target, token === "W");
        return { pos: target };
    case "b": case "B":
        for (i = 0; i < count; ++i)
            target = wordBackward(t, target, token === "B");
        return { pos: target };
    case "e": case "E":
        for (i = 0; i < count; ++i)
            target = wordEnd(t, target, token === "E");
        return { pos: target, inclusive: true };
    case "}":
        for (i = 0; i < count; ++i)
            target = paragraphForward(t, target);
        return { pos: target };
    case "{":
        for (i = 0; i < count; ++i)
            target = paragraphBackward(t, target);
        return { pos: target };
    case "%":
        target = matchPair(t, p);
        return target < 0 ? { pos: p } : { pos: target, inclusive: true };
    case "<C-d>": case "<C-u>": case "<C-f>": case "<C-b>": case "<PageDown>": case "<PageUp>":
        // The editor pages by what is on screen, so wrapped paragraphs count
        // as the lines they show; without it, fall back to text lines.
        var fraction = { "<C-d>": 0.5, "<C-u>": -0.5, "<C-f>": 1, "<C-b>": -1,
                         "<PageDown>": 1, "<PageUp>": -1 }[token];
        if (ed.scrollPage)
            return { pos: clampNormal(t, ed.scrollPage(fraction, p)), linewise: true };
        var pageLines = Math.max(1, Math.round(ed.pageLines() * Math.abs(fraction)));
        var fromLine = lineIndex(t, p);
        var toLine = Math.max(0, Math.min(lineCount(t) - 1, fromLine + (fraction > 0 ? pageLines : -pageLines)));
        return { pos: firstNonBlank(t, startOfLine(t, toLine)), linewise: true };
    case "j": case "<Down>": case "<CR>": case "+":
    case "k": case "<Up>": case "-":
        var down = token === "j" || token === "<Down>" || token === "<CR>" || token === "+";
        var lines = count;
        var current = lineIndex(t, p);
        var wanted = Math.max(0, Math.min(lineCount(t) - 1, current + (down ? lines : -lines)));
        var column = vim.column >= 0 ? vim.column : p - lineStart(t, p);
        vim.column = column;
        var start = startOfLine(t, wanted);
        if (token === "<CR>" || token === "+" || token === "-")
            return { pos: firstNonBlank(t, start), linewise: true };
        return { pos: Math.min(start + column, lastChar(t, start)), linewise: true, keepColumn: true };
    case "G":
        target = vim.count !== "" || count > 1 ? startOfLine(t, count - 1) : startOfLine(t, lineCount(t) - 1);
        return { pos: firstNonBlank(t, target), linewise: true };
    case "gg":
        return { pos: firstNonBlank(t, startOfLine(t, count - 1)), linewise: true };
    case ";": case ",":
        if (!vim.lastFind)
            return { pos: p };
        var kind = vim.lastFind.kind;
        if (token === ",")
            kind = { f: "F", F: "f", t: "T", T: "t" }[kind];
        for (i = 0; i < count; ++i) {
            var found = findInLine(t, target, vim.lastFind.ch, kind);
            if (found < 0)
                break;
            target = found;
        }
        return { pos: target, inclusive: kind === "f" || kind === "t" };
    }
    if (token.length === 2 && "fFtT".indexOf(token[0]) >= 0) {
        vim.lastFind = { kind: token[0], ch: token[1] };
        for (i = 0; i < count; ++i) {
            var hit = findInLine(t, target, token[1], token[0]);
            if (hit < 0)
                break;
            target = hit;
        }
        return { pos: target, inclusive: token[0] === "f" || token[0] === "t" };
    }
    return null;
}

// ---- editing ------------------------------------------------------------

function setMode(vim, ed, mode) {
    vim.mode = mode;
    if (mode !== VISUAL && mode !== VISUAL_LINE)
        ed.clearSelection();
    if (ed.host && ed.host.modeChanged)
        ed.host.modeChanged(mode);
}

function enterInsert(vim, ed, pos) {
    ed.setCursor(pos);
    vim.insertStart = pos;
    vim.insertLength = ed.text().length;
    vim.pendingInsertKeys = vim.keys.slice();
    setMode(vim, ed, INSERT);
}

function leaveInsert(vim, ed) {
    var t = ed.text(), p = ed.cursor();
    var typed = p >= vim.insertStart && t.length - vim.insertLength === p - vim.insertStart
        ? t.slice(vim.insertStart, p) : null;
    if (vim.pendingInsertKeys && !vim.replaying)
        vim.lastChange = { keys: vim.pendingInsertKeys, insert: typed };
    vim.pendingInsertKeys = null;
    setMode(vim, ed, NORMAL);
    ed.setCursor(Math.max(lineStart(t, p), p - 1));
}

function remember(vim) {
    if (!vim.replaying)
        vim.lastChange = { keys: vim.keys.slice(), insert: null };
}

function linewiseRange(t, start, end) {
    var s = lineStart(t, start);
    var e = lineEnd(t, Math.max(start, end));
    return [s, e];
}

function applyOperator(vim, ed, op, start, end, linewise) {
    var t = ed.text();
    if (linewise) {
        var range = linewiseRange(t, start, end);
        start = range[0];
        end = range[1];
    }
    var text = t.slice(start, end) + (linewise ? "\n" : "");
    if (op === "y") {
        // Yanking whole lines leaves the cursor where it was, as in vim.
        vim.register = { text: text, linewise: linewise };
        if (!linewise)
            ed.setCursor(start);
        return;
    }
    if (op === "d" || op === "c") {
        vim.register = { text: text, linewise: linewise };
        if (linewise && op === "d") {
            // Take the line's newline with it; the last line takes the one before.
            var from = start, to = end;
            if (end < t.length)
                to = end + 1;
            else if (start > 0)
                from = start - 1;
            ed.replace(from, to, "");
            var after = ed.text();
            ed.setCursor(firstNonBlank(after, lineStart(after, Math.min(from, after.length))));
        } else if (linewise) {
            var indent = t.slice(start, firstNonBlank(t, start));
            ed.replace(start, end, indent);
            enterInsert(vim, ed, start + indent.length);
            return;
        } else {
            ed.replace(start, end, "");
            if (op === "c") {
                enterInsert(vim, ed, start);
                return;
            }
            ed.setCursor(clampNormal(ed.text(), start));
        }
        return;
    }
    if (op === ">" || op === "<") {
        var lines = t.slice(start, end).split("\n").map(function(line) {
            if (op === ">")
                return line.length > 0 ? shiftWidth + line : line;
            if (line.indexOf(shiftWidth) === 0)
                return line.slice(shiftWidth.length);
            return line.replace(/^[ \t]/, "");
        });
        ed.replace(start, end, lines.join("\n"));
        ed.setCursor(firstNonBlank(ed.text(), start));
        return;
    }
    if (op === "~" || op === "u" || op === "U") {
        var changed = t.slice(start, end).split("").map(function(c) {
            if (op === "u") return c.toLowerCase();
            if (op === "U") return c.toUpperCase();
            return c === c.toUpperCase() ? c.toLowerCase() : c.toUpperCase();
        }).join("");
        ed.replace(start, end, changed);
        ed.setCursor(clampNormal(ed.text(), start));
    }
}

function put(vim, ed, before, count) {
    var t = ed.text(), p = ed.cursor();
    var text = "";
    for (var i = 0; i < count; ++i)
        text += vim.register.text;
    if (text === "")
        return;
    if (vim.register.linewise) {
        var at = before ? lineStart(t, p) : lineEnd(t, p);
        if (before) {
            ed.replace(at, at, text);
            ed.setCursor(firstNonBlank(ed.text(), at));
        } else {
            ed.replace(at, at, "\n" + text.slice(0, -1));
            ed.setCursor(firstNonBlank(ed.text(), at + 1));
        }
    } else {
        var pos = before || lineEnd(t, p) === p ? p : p + 1;
        ed.replace(pos, pos, text);
        ed.setCursor(pos + text.length - 1);
    }
}

function joinLines(ed, p, count) {
    var t = ed.text();
    for (var i = 0; i < Math.max(1, count - 1); ++i) {
        t = ed.text();
        var e = lineEnd(t, p);
        if (e >= t.length)
            break;
        var next = e + 1;
        while (next < t.length && (t[next] === " " || t[next] === "\t"))
            ++next;
        var glue = next < t.length && t[next] !== "\n" && e > lineStart(t, e) ? " " : "";
        ed.replace(e, next, glue);
        ed.setCursor(e);
    }
}

function searchFrom(t, query, from, forward) {
    if (!query)
        return -1;
    var haystack = t.toLowerCase(), needle = query.toLowerCase();
    var i = forward ? haystack.indexOf(needle, from + 1) : haystack.lastIndexOf(needle, from - 1);
    if (i < 0)
        i = forward ? haystack.indexOf(needle) : haystack.lastIndexOf(needle);
    return i;
}

// The search typed after "/" lands here from the find bar.
function searchCommitted(vim, ed, query) {
    vim.lastSearch = { query: query, forward: true };
    var hit = searchFrom(ed.text(), query, ed.cursor(), true);
    if (hit >= 0)
        ed.setCursor(hit);
    else if (ed.host)
        ed.host.message("Pattern not found: " + query);
}

// ":" commands from the command line.
function runCommand(vim, ed, command) {
    var c = command.trim();
    if (/^\d+$/.test(c)) {
        ed.setCursor(firstNonBlank(ed.text(), startOfLine(ed.text(), Math.max(0, parseInt(c, 10) - 1))));
        return;
    }
    switch (c) {
    case "w": ed.host.save(); return;
    case "q": ed.host.quit(false); return;
    case "q!": ed.host.quit(true); return;
    case "wq": case "x": ed.host.save(); ed.host.quit(false); return;
    case "": return;
    }
    ed.host.message("Not an editor command: " + c);
}

function updateVisual(vim, ed) {
    var t = ed.text();
    var a = Math.min(vim.anchor, vim.cursor), b = Math.max(vim.anchor, vim.cursor);
    if (vim.mode === VISUAL_LINE)
        ed.select(lineStart(t, a), lineEnd(t, b));
    else
        ed.select(a, Math.min(t.length, b + 1));
}

function visualRange(vim, ed) {
    var t = ed.text();
    var a = Math.min(vim.anchor, vim.cursor), b = Math.max(vim.anchor, vim.cursor);
    return vim.mode === VISUAL_LINE ? [a, b, true] : [a, Math.min(t.length, b + 1), false];
}

function repeatLast(vim, ed) {
    if (!vim.lastChange)
        return;
    var change = vim.lastChange;
    vim.replaying = true;
    for (var i = 0; i < change.keys.length; ++i)
        feed(vim, ed, change.keys[i]);
    if (vim.mode === INSERT) {
        if (change.insert)
            ed.replace(ed.cursor(), ed.cursor(), change.insert);
        leaveInsert(vim, ed);
    }
    vim.replaying = false;
}

// ---- the key handler ----------------------------------------------------

function reset(vim) {
    vim.count = "";
    vim.operator = "";
    vim.operatorCount = 1;
    vim.prefix = "";
}

// Returns true when the key was handled; in insert mode only Escape is.
function feed(vim, ed, token) {
    if (vim.mode === INSERT) {
        if (token === "<Esc>" || token === "<C-[>" || token === "<C-c>") {
            leaveInsert(vim, ed);
            return true;
        }
        return false;
    }

    if (vim.prefix === "" && vim.count === "" && vim.operator === "")
        vim.keys = [];
    vim.keys.push(token);
    var visual = vim.mode === VISUAL || vim.mode === VISUAL_LINE;
    var t = ed.text();
    var p = visual ? vim.cursor : ed.cursor();

    if (token === "<Esc>" || token === "<C-[>" || token === "<C-c>") {
        reset(vim);
        if (visual) {
            setMode(vim, ed, NORMAL);
            ed.setCursor(clampNormal(t, vim.cursor));
        }
        return true;
    }

    // Keys that wait for one more key.
    if (vim.prefix !== "") {
        var prefix = vim.prefix;
        vim.prefix = "";
        if (prefix === "r") {
            var n = Math.max(1, parseInt(vim.count || "1", 10));
            if (token.length === 1 && p + n <= lineEnd(t, p)) {
                ed.replace(p, p + n, new Array(n + 1).join(token));
                ed.setCursor(p + n - 1);
                remember(vim);
            }
            reset(vim);
            return true;
        }
        if (prefix === "i" || prefix === "a") {
            var range = textObject(t, p, prefix === "a", token);
            if (range && vim.operator) {
                applyOperator(vim, ed, vim.operator, range[0], range[1], false);
                if (vim.operator !== "y")
                    remember(vim);
            } else if (range && visual) {
                vim.anchor = range[0];
                vim.cursor = Math.max(range[0], range[1] - 1);
                updateVisual(vim, ed);
            }
            reset(vim);
            return true;
        }
        token = prefix + token;
    }

    if (/^[1-9]$/.test(token) || (token === "0" && vim.count !== "")) {
        vim.count += token;
        return true;
    }
    var count = Math.max(1, parseInt(vim.count || "1", 10));

    var waitsForKey = token === "g" || (token.length === 1 && "fFtT".indexOf(token) >= 0)
        || (token === "r" && !visual)
        || ((token === "i" || token === "a") && (vim.operator !== "" || visual));
    if (waitsForKey) {
        vim.prefix = token;
        return true;
    }

    var m = motion(vim, ed, token, count * (vim.operator ? vim.operatorCount : 1));
    if (m) {
        if (!m.keepColumn)
            vim.column = -1;
        if (vim.operator) {
            var op = vim.operator;
            var start = Math.min(p, m.pos), end = Math.max(p, m.pos);
            if (m.inclusive)
                ++end;
            // "cw" changes to the end of the word, like "ce".
            if (op === "c" && (token === "w" || token === "W") && charClass(t[p], token === "W") !== 0) {
                end = wordEnd(t, p, token === "W") + 1;
                for (var k = 1; k < count; ++k)
                    end = wordEnd(t, end - 1, token === "W") + 1;
            } else if ((token === "w" || token === "W") && end > lineEnd(t, p) && lineEnd(t, p) > p) {
                end = lineEnd(t, p);
            }
            applyOperator(vim, ed, op, start, end, !!m.linewise);
            if (op !== "y")
                remember(vim);
            reset(vim);
            return true;
        }
        if (visual) {
            vim.cursor = m.pos;
            updateVisual(vim, ed);
        } else {
            ed.setCursor(m.linewise || token === "$" ? m.pos : clampNormal(t, m.pos));
        }
        reset(vim);
        return true;
    }

    if (visual)
        return visualCommand(vim, ed, token, count);

    if (vim.operator) {
        // dd, cc, yy, >>, << work on whole lines.
        if (token === vim.operator) {
            var lastLine = startOfLine(t, Math.min(lineCount(t) - 1, lineIndex(t, p) + count - 1));
            applyOperator(vim, ed, vim.operator, lineStart(t, p), lastLine, true);
            if (vim.operator !== "y")
                remember(vim);
        }
        reset(vim);
        return true;
    }

    return normalCommand(vim, ed, token, count, t, p);
}

function normalCommand(vim, ed, token, count, t, p) {
    switch (token) {
    case "d": case "c": case "y": case ">": case "<":
        vim.operator = token;
        vim.operatorCount = count;
        vim.count = "";
        return true;
    case "g~": case "gu": case "gU":
        vim.operator = token.slice(1) === "~" ? "~" : token.slice(1);
        vim.operatorCount = count;
        vim.count = "";
        return true;
    case "i": enterInsert(vim, ed, p); break;
    case "a": enterInsert(vim, ed, Math.min(p + 1, lineEnd(t, p))); break;
    case "I": enterInsert(vim, ed, firstNonBlank(t, p)); break;
    case "A": enterInsert(vim, ed, lineEnd(t, p)); break;
    case "o": case "O":
        var indent = t.slice(lineStart(t, p), firstNonBlank(t, p));
        if (token === "o") {
            var e = lineEnd(t, p);
            ed.replace(e, e, "\n" + indent);
            enterInsert(vim, ed, e + 1 + indent.length);
        } else {
            var s = lineStart(t, p);
            ed.replace(s, s, indent + "\n");
            enterInsert(vim, ed, s + indent.length);
        }
        break;
    case "x": case "<Del>":
        var xe = Math.min(lineEnd(t, p), p + count);
        if (xe > p) {
            vim.register = { text: t.slice(p, xe), linewise: false };
            ed.replace(p, xe, "");
            ed.setCursor(clampNormal(ed.text(), p));
            remember(vim);
        }
        break;
    case "X":
        var xs = Math.max(lineStart(t, p), p - count);
        if (xs < p) {
            vim.register = { text: t.slice(xs, p), linewise: false };
            ed.replace(xs, p, "");
            ed.setCursor(xs);
            remember(vim);
        }
        break;
    case "s":
        ed.replace(p, Math.min(lineEnd(t, p), p + count), "");
        enterInsert(vim, ed, p);
        break;
    case "S":
        applyOperator(vim, ed, "c", lineStart(t, p),
                      startOfLine(t, Math.min(lineCount(t) - 1, lineIndex(t, p) + count - 1)), true);
        break;
    case "C":
        applyOperator(vim, ed, "c", p, lineEnd(t, p), false);
        break;
    case "D":
        applyOperator(vim, ed, "d", p, lineEnd(t, p), false);
        remember(vim);
        break;
    case "Y":
        applyOperator(vim, ed, "y", lineStart(t, p), lineStart(t, p), true);
        break;
    case "~":
        var te = Math.min(lineEnd(t, p), p + count);
        applyOperator(vim, ed, "~", p, te, false);
        ed.setCursor(clampNormal(ed.text(), te));
        remember(vim);
        break;
    case "J":
        joinLines(ed, p, Math.max(2, count));
        remember(vim);
        break;
    case "p": case "P":
        put(vim, ed, token === "P", count);
        remember(vim);
        break;
    case "u":
        for (var u = 0; u < count; ++u)
            ed.undo();
        ed.setCursor(clampNormal(ed.text(), ed.cursor()));
        break;
    case "<C-r>":
        for (var r = 0; r < count; ++r)
            ed.redo();
        ed.setCursor(clampNormal(ed.text(), ed.cursor()));
        break;
    case ".":
        repeatLast(vim, ed);
        break;
    case "v": case "V":
        vim.anchor = p;
        vim.cursor = p;
        setMode(vim, ed, token === "v" ? VISUAL : VISUAL_LINE);
        updateVisual(vim, ed);
        break;
    case "/":
        if (ed.host) ed.host.openSearch();
        break;
    case ":":
        if (ed.host) ed.host.openCommand();
        break;
    case "n": case "N":
        if (vim.lastSearch) {
            var forward = vim.lastSearch.forward === (token === "n");
            for (var c = 0, at = p; c < count; ++c) {
                var hit = searchFrom(ed.text(), vim.lastSearch.query, at, forward);
                if (hit < 0) break;
                at = hit;
            }
            if (hit >= 0) ed.setCursor(at);
        }
        break;
    case "*": case "#":
        var word = textObject(t, p, false, "w");
        if (word && word[1] > word[0]) {
            vim.lastSearch = { query: t.slice(word[0], word[1]), forward: token === "*" };
            var found = searchFrom(t, vim.lastSearch.query, word[0], token === "*");
            if (found >= 0) ed.setCursor(found);
        }
        break;
    case "gJ":
        joinLines(ed, p, Math.max(2, count));
        break;
    case "<C-e>": case "<C-y>":
        if (ed.scrollLines) {
            var kept = ed.scrollLines(token === "<C-e>" ? count : -count);
            if (kept >= 0)
                ed.setCursor(clampNormal(ed.text(), kept));
        }
        break;
    default:
        break;
    }
    reset(vim);
    return true;
}

function visualCommand(vim, ed, token, count) {
    var range = visualRange(vim, ed);
    var t = ed.text();
    switch (token) {
    case "o":
        var swap = vim.anchor;
        vim.anchor = vim.cursor;
        vim.cursor = swap;
        updateVisual(vim, ed);
        reset(vim);
        return true;
    case "v": case "V":
        var wanted = token === "v" ? VISUAL : VISUAL_LINE;
        if (vim.mode === wanted) {
            setMode(vim, ed, NORMAL);
            ed.setCursor(clampNormal(t, vim.cursor));
        } else {
            vim.mode = wanted;
            updateVisual(vim, ed);
        }
        reset(vim);
        return true;
    case "d": case "x": case "<Del>": case "y": case "c": case "s":
    case ">": case "<": case "~": case "u": case "U":
        var op = { x: "d", "<Del>": "d", s: "c" }[token] || token;
        setMode(vim, ed, NORMAL);
        applyOperator(vim, ed, op, range[0], range[1], range[2]);
        break;
    case "D": case "X": case "Y": case "C": case "S": case "R":
        var lineOp = { D: "d", X: "d", Y: "y", C: "c", S: "c", R: "c" }[token];
        setMode(vim, ed, NORMAL);
        applyOperator(vim, ed, lineOp, range[0], range[1], true);
        break;
    case "J":
        setMode(vim, ed, NORMAL);
        joinLines(ed, range[0], Math.max(2, lineIndex(t, range[1]) - lineIndex(t, range[0]) + 1));
        break;
    case "p": case "P":
        var saved = vim.register;
        setMode(vim, ed, NORMAL);
        applyOperator(vim, ed, "d", range[0], range[1], range[2]);
        vim.register = saved;
        put(vim, ed, true, 1);
        break;
    case ":":
        setMode(vim, ed, NORMAL);
        if (ed.host) ed.host.openCommand();
        break;
    default:
        reset(vim);
        return true;
    }
    reset(vim);
    return true;
}
