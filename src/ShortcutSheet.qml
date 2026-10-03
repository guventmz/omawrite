import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Every shortcut in one place, and where to change them. Type to filter,
// move with the arrows or Ctrl+N / Ctrl+P, press Enter (or click the keys)
// and then the new keys; Backspace on an empty filter restores the default.
// The vim and file tree keys are listed for reference.
Popup {
    id: sheet

    property bool macOS: false
    property real textScale: 1
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"
    property color headingColor: "#ffffff"
    property color highlightColor: "#428bca"
    property color surfaceColor: "#22221f"
    property real maximumHeight: 640
    readonly property real headerHeight: scaledSize(32)

    property int selected: 0
    property string recordingId: ""
    property string note: ""

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    // Shown alongside the remappable actions: [title, macOS keys, other keys].
    readonly property var extras: ({
        "Notes": [["Insert link", "[[", "[["],
                  ["Down / up in any list", "⌃n  ⌃p", "Ctrl+N  Ctrl+P"],
                  ["Paste / drop an image", "⌘V  drag in", "Ctrl+V  drag in"]],
        "Editing": [["Previous match", "⇧↵", "Shift+Enter"],
                    ["Spelling suggestions", "right-click", "right-click"]]
    })

    readonly property var fixedGroups: [
        { title: "File tree (netrw keys)", items: [
            ["Down / up", "j k  ⌃n ⌃p", "j k  Ctrl+N Ctrl+P"],
            ["Open note / fold folder", "↵  o v t", "Enter  o v t"],
            ["Expand / collapse", "l h  → ←", "l h  Right Left"],
            ["Up to parent folder", "-", "-"],
            ["Top / bottom", "gg  G", "gg  G"],
            ["Search the whole vault", "/", "/"],
            ["New note", "%  n", "%  n"],
            ["New folder", "d  ⇧N", "d  Shift+N"],
            ["Rename", "R  r", "R  r  F2"],
            ["Move to Trash", "D  ⌘⌫", "D  Del"],
            ["Move via folder picker", "M", "M"],
            ["Mark / target / move marked", "mf  mt  mm", "mf  mt  mm"],
            ["Unmark all", "mu", "mu"],
            ["Refresh / close", "⌃l  Esc", "Ctrl+L  Esc"]
        ]},
        { title: "Vim (when on)", items: [
            ["Insert / append", "i a I A o O", "i a I A o O"],
            ["Back to normal", "Esc  ⌃[", "Esc  Ctrl+["],
            ["Move", "h j k l  w b e  0 ^ $", "h j k l  w b e  0 ^ $"],
            ["Jump", "gg G  { }  %  f t ; ,", "gg G  { }  %  f t ; ,"],
            ["Half page / page", "⌃d ⌃u  ⌃f ⌃b", "Ctrl+D  Ctrl+U"],
            ["Scroll a line", "⌃e  ⌃y", "Ctrl+E"],
            ["Delete / change / yank", "d c y + motion", "d c y + motion"],
            ["Whole line", "dd cc yy >> <<", "dd cc yy >> <<"],
            ["Inside / around", "iw aw i\" i( i[ i{ ip", "iw aw i\" i( i[ i{ ip"],
            ["Edit", "x r ~ J p P", "x r ~ J p P"],
            ["Undo / redo / repeat", "u  ⌃r  .", "u  Ctrl+R  ."],
            ["Visual / line", "v  V", "v  V"],
            ["Search", "/  n N  * #", "/  n N  * #"],
            ["Save, close, go to line", ":w :q :wq :q! :42", ":w :q :wq :q! :42"]
        ]}
    ]

    // Groups of rows; each row is { title, keys, id, custom, index }, where
    // only rows with an id (keymap actions) can be changed and index is the
    // row's place in the flat list the arrows move through.
    readonly property var groups: {
        var query = filter.text.trim().toLocaleLowerCase();
        var labels = keymap.labels;
        var byGroup = {};
        var order = [];
        var add = function(group, row) {
            if (!byGroup[group]) {
                byGroup[group] = [];
                order.push(group);
            }
            if (query.length > 0 && row.title.toLocaleLowerCase().indexOf(query) === -1
                    && row.keys.toLocaleLowerCase().indexOf(query) === -1
                    && group.toLocaleLowerCase().indexOf(query) === -1)
                return;
            byGroup[group].push(row);
        };
        var actions = keymap.actions;
        for (var i = 0; i < actions.length; ++i) {
            var action = actions[i];
            add(action.group, { title: action.title, keys: labels[action.id], id: action.id,
                                custom: keymap.isCustom(action.id) });
        }
        for (var group in extras) {
            for (var e = 0; e < extras[group].length; ++e) {
                var extra = extras[group][e];
                add(group, { title: extra[0], keys: macOS ? extra[1] : extra[2], id: "", custom: false });
            }
        }
        for (var f = 0; f < fixedGroups.length; ++f) {
            for (var r = 0; r < fixedGroups[f].items.length; ++r) {
                var item = fixedGroups[f].items[r];
                add(fixedGroups[f].title, { title: item[0], keys: macOS ? item[1] : item[2], id: "", custom: false });
            }
        }
        var result = [];
        var index = 0;
        for (var o = 0; o < order.length; ++o) {
            var rows = byGroup[order[o]];
            if (rows.length === 0)
                continue;
            for (var k = 0; k < rows.length; ++k)
                rows[k].index = index++;
            result.push({ title: order[o], items: rows });
        }
        return result;
    }

    readonly property var flatRows: {
        var rows = [];
        for (var g = 0; g < groups.length; ++g)
            rows = rows.concat(groups[g].items);
        return rows;
    }

    readonly property var selectedRow: selected >= 0 && selected < flatRows.length ? flatRows[selected] : null

    function move(step) {
        selected = Math.max(0, Math.min(flatRows.length - 1, selected + step));
    }

    function startRecording(id) {
        if (id === "") {
            note = "This one can't be changed here";
            return;
        }
        recordingId = id;
        keymap.recording = true;
        note = "Press the new keys · Esc cancels";
        recorder.forceActiveFocus();
    }

    function stopRecording() {
        recordingId = "";
        keymap.recording = false;
        filter.forceActiveFocus();
    }

    function record(key, modifiers) {
        var sequence = keymap.sequenceFor(key, modifiers);
        if (sequence === "")
            return;
        var id = recordingId;
        var lost = keymap.assign(id, sequence);
        stopRecording();
        note = lost !== "" ? "Moved " + keymap.labels[id] + " from “" + lost + "”" : "Saved";
    }

    modal: true
    focus: true
    padding: 20
    closePolicy: recordingId !== "" ? Popup.NoAutoClose
                                    : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.elevation: 12

    onAboutToShow: {
        filter.text = "";
        selected = 0;
        note = "";
        filter.forceActiveFocus();
    }
    onClosed: {
        if (recordingId !== "")
            stopRecording();
    }

    background: Rectangle {
        radius: 12
        color: sheet.surfaceColor
        border.width: 1
        border.color: Qt.rgba(0.5, 0.5, 0.5, 0.25)
    }

    contentItem: Column {
        spacing: sheet.scaledSize(14)

        // Takes the keys while a shortcut is being recorded.
        Item {
            id: recorder
            width: 0
            height: 0
            Keys.onPressed: function(event) {
                event.accepted = true;
                if (event.key === Qt.Key_Escape) {
                    sheet.stopRecording();
                    sheet.note = "";
                    return;
                }
                sheet.record(event.key, event.modifiers);
            }
        }

        Item {
            width: parent.width
            height: sheet.headerHeight

            Label {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Keyboard shortcuts"
                color: sheet.headingColor
                font.family: "IBM Plex Mono"
                font.bold: true
                font.pixelSize: sheet.scaledSize(16)
            }

            TextInput {
                id: filter
                objectName: "shortcutFilter"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: sheet.scaledSize(220)
                color: sheet.textColor
                selectionColor: sheet.highlightColor
                font.family: "IBM Plex Mono"
                font.pixelSize: sheet.scaledSize(13)
                clip: true
                onTextChanged: sheet.selected = 0

                Keys.onPressed: function(event) {
                    var control = sheet.macOS ? (event.modifiers & Qt.MetaModifier) : (event.modifiers & Qt.ControlModifier);
                    if (event.key === Qt.Key_Down || (control && event.key === Qt.Key_N)) {
                        sheet.move(1);
                    } else if (event.key === Qt.Key_Up || (control && event.key === Qt.Key_P)) {
                        sheet.move(-1);
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (sheet.selectedRow)
                            sheet.startRecording(sheet.selectedRow.id);
                    } else if (event.key === Qt.Key_Backspace && filter.text.length === 0
                               && sheet.selectedRow && sheet.selectedRow.custom) {
                        keymap.reset(sheet.selectedRow.id);
                        sheet.note = "Restored the default";
                    } else {
                        return;
                    }
                    event.accepted = true;
                }

                Label {
                    anchors.fill: parent
                    text: "Type to filter"
                    visible: filter.text.length === 0
                    color: sheet.mutedColor
                    font: filter.font
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.bottom
                    anchors.topMargin: 4
                    height: 1
                    color: sheet.mutedColor
                    opacity: 0.4
                }
            }
        }

        Flickable {
            id: body
            width: parent.width
            height: Math.min(columns.implicitHeight, sheet.maximumHeight - sheet.topPadding
                             - sheet.bottomPadding - sheet.headerHeight - footer.height - 2 * parent.spacing)
            contentHeight: columns.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Flow {
                id: columns
                width: parent.width
                spacing: sheet.scaledSize(28)

                Repeater {
                    model: sheet.groups

                    delegate: Column {
                        id: group
                        required property var modelData
                        width: (columns.width - columns.spacing) / 2
                        spacing: sheet.scaledSize(4)

                        Label {
                            text: group.modelData.title
                            color: sheet.headingColor
                            font.family: "IBM Plex Mono"
                            font.bold: true
                            font.pixelSize: sheet.scaledSize(12)
                            bottomPadding: sheet.scaledSize(2)
                        }

                        Repeater {
                            model: group.modelData.items

                            delegate: Rectangle {
                                id: shortcut
                                required property var modelData
                                readonly property bool isSelected: modelData.index === sheet.selected
                                readonly property bool isRecording: modelData.id !== "" && modelData.id === sheet.recordingId

                                width: group.width
                                height: sheet.scaledSize(24)
                                radius: 4
                                color: isSelected ? Qt.rgba(0.5, 0.5, 0.5, 0.14) : "transparent"

                                // Keep the selected row in view as the arrows move.
                                onIsSelectedChanged: {
                                    if (!isSelected)
                                        return;
                                    var point = mapToItem(columns, 0, 0);
                                    if (point.y < body.contentY)
                                        body.contentY = point.y;
                                    else if (point.y + height > body.contentY + body.height)
                                        body.contentY = point.y + height - body.height;
                                }

                                Label {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 6
                                    anchors.right: keys.left
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: shortcut.modelData.title
                                    elide: Text.ElideRight
                                    color: shortcut.modelData.id !== "" ? sheet.textColor : sheet.mutedColor
                                    font.family: "IBM Plex Mono"
                                    font.pixelSize: sheet.scaledSize(12)
                                }

                                Rectangle {
                                    id: keys
                                    anchors.right: parent.right
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: keyLabel.implicitWidth + sheet.scaledSize(12)
                                    height: sheet.scaledSize(20)
                                    radius: 4
                                    color: shortcut.isRecording ? sheet.highlightColor : Qt.rgba(0.5, 0.5, 0.5, 0.16)

                                    Label {
                                        id: keyLabel
                                        anchors.centerIn: parent
                                        text: shortcut.isRecording ? "press keys…"
                                            : (shortcut.modelData.custom ? "• " : "") + shortcut.modelData.keys
                                        color: shortcut.modelData.custom && !shortcut.isRecording
                                               ? sheet.highlightColor : sheet.textColor
                                        font.family: "IBM Plex Mono"
                                        font.pixelSize: sheet.scaledSize(12)
                                    }

                                    TapHandler {
                                        enabled: shortcut.modelData.id !== ""
                                        onTapped: {
                                            sheet.selected = shortcut.modelData.index;
                                            sheet.startRecording(shortcut.modelData.id);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Label {
            id: footer
            width: parent.width
            text: sheet.note !== "" ? sheet.note
                : "↵ change selected  ·  ⌫ restore default  ·  • changed  ·  esc close"
            color: sheet.mutedColor
            font.family: "IBM Plex Mono"
            font.pixelSize: sheet.scaledSize(11)
        }
    }
}
