import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Every shortcut in one place, grouped, with a filter: type a word ("link",
// "size") or a key to narrow the list.
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

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    // [description, macOS keys, other keys]
    readonly property var groups: [
        { title: "Notes", items: [
            ["Go to or create note", "⌘P", "Ctrl+P"],
            ["Search all notes", "⇧⌘F", "Ctrl+Shift+F"],
            ["Backlinks", "⇧⌘B", "Ctrl+Shift+B"],
            ["Follow [[link]]", "⌘↩  ⌘-click", "Ctrl+Enter  Ctrl-click"],
            ["Back / forward", "⌘[  ⌘]", "Ctrl+[  Ctrl+]"],
            ["Insert link", "[[", "[["],
            ["File tree", "⇧⌘E", "Ctrl+Shift+E"],
            ["Preview (browser window)", "⇧⌘P", "Ctrl+Shift+P"],
            ["Choose vault folder", "⇧⌘O", "Ctrl+Shift+O"],
            ["Down / up in any list", "⌃n  ⌃p", "Ctrl+N  Ctrl+P"]
        ]},
        { title: "File tree (netrw keys)", items: [
            ["Down / up", "j k  ⌃n ⌃p", "j k  Ctrl+N Ctrl+P"],
            ["Open note / fold folder", "↵  o v t", "Enter  o v t"],
            ["Expand / collapse", "l h  → ←", "l h  Right Left"],
            ["Up to parent folder", "-", "-"],
            ["Top / bottom", "gg  G", "gg  G"],
            ["New note", "%  n", "%  n"],
            ["New folder", "d  ⇧N", "d  Shift+N"],
            ["Rename", "R  r", "R  r  F2"],
            ["Move to Trash", "D  ⌘⌫", "D  Del"],
            ["Move via folder picker", "M", "M"],
            ["Mark / target / move marked", "mf  mt  mm", "mf  mt  mm"],
            ["Unmark all", "mu", "mu"],
            ["Refresh / close", "⌃l  Esc", "Ctrl+L  Esc"]
        ]},
        { title: "Editing", items: [
            ["Bold", "⌘B", "Ctrl+B"],
            ["Italic", "⌘I", "Ctrl+I"],
            ["Markdown link", "⌘K", "Ctrl+K"],
            ["Undo / redo", "⌘Z  ⇧⌘Z", "Ctrl+Z  Ctrl+Shift+Z"],
            ["Find", "⌘F", "Ctrl+F"],
            ["Next / previous match", "↵  ⇧↵", "Enter  Shift+Enter"],
            ["Find and replace", "⌥⌘F", "Ctrl+H"],
            ["Look up word", "⌃⌘D", "—"],
            ["Spelling suggestions", "right-click", "right-click"]
        ]},
        { title: "View", items: [
            ["Theme", "⌥⌘T", "Ctrl+Alt+T"],
            ["Wrap: column / window / off", "⌥⌘L", "Ctrl+Alt+L"],
            ["Text size", "⌘=  ⌘−  ⌘0", "Ctrl+=  Ctrl+-  Ctrl+0"],
            ["Interface size", "⌥⌘=  ⌥⌘−  ⌥⌘0", "Ctrl+Alt+=  -  0"],
            ["Fullscreen", "⌃⌘F", "F11"],
            ["This sheet", "⌘,  ⌘?", "Ctrl+,  Ctrl+?"],
            ["Vim mode on / off", "⌥⌘V", "Ctrl+Alt+V"]
        ]},
        { title: "Vim (when on)", items: [
            ["Insert / append", "i a I A o O", "i a I A o O"],
            ["Back to normal", "Esc  ⌃[", "Esc  Ctrl+["],
            ["Move", "h j k l  w b e  0 ^ $", "h j k l  w b e  0 ^ $"],
            ["Jump", "gg G  { }  %  f t ; ,", "gg G  { }  %  f t ; ,"],
            ["Half page", "⌃d  ⌃u", "Ctrl+D  Ctrl+U"],
            ["Delete / change / yank", "d c y + motion", "d c y + motion"],
            ["Whole line", "dd cc yy >> <<", "dd cc yy >> <<"],
            ["Inside / around", "iw aw i\" i( i[ i{ ip", "iw aw i\" i( i[ i{ ip"],
            ["Edit", "x r ~ J p P", "x r ~ J p P"],
            ["Undo / redo / repeat", "u  ⌃r  .", "u  Ctrl+R  ."],
            ["Visual / line", "v  V", "v  V"],
            ["Search", "/  n N  * #", "/  n N  * #"],
            ["Save, close, go to line", ":w :q :wq :q! :42", ":w :q :wq :q! :42"]
        ]},
        { title: "File", items: [
            ["Save", "⌘S", "Ctrl+S"],
            ["Save as", "⇧⌘S", "Ctrl+Shift+S"],
            ["Open file", "⌘O", "Ctrl+O"],
            ["New window", "⌘N", "Ctrl+N"],
            ["Close window", "⌘W", "—"],
            ["Minimize", "⌘M", "—"],
            ["Print", "⌥⌘P", "Ctrl+Alt+P"]
        ]}
    ]

    readonly property var filteredGroups: {
        var query = filter.text.trim().toLocaleLowerCase();
        var result = [];
        for (var g = 0; g < groups.length; ++g) {
            var items = groups[g].items.filter(function(item) {
                var keys = macOS ? item[1] : item[2];
                return keys !== "—" && (query.length === 0
                    || item[0].toLocaleLowerCase().indexOf(query) !== -1
                    || keys.toLocaleLowerCase().indexOf(query) !== -1
                    || groups[g].title.toLocaleLowerCase().indexOf(query) !== -1);
            });
            if (items.length > 0)
                result.push({ title: groups[g].title, items: items });
        }
        return result;
    }

    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.elevation: 12

    onAboutToShow: {
        filter.text = "";
        filter.forceActiveFocus();
    }

    background: Rectangle {
        radius: 12
        color: sheet.surfaceColor
        border.width: 1
        border.color: Qt.rgba(0.5, 0.5, 0.5, 0.25)
    }

    contentItem: Column {
        spacing: sheet.scaledSize(14)

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
            width: parent.width
            height: Math.min(columns.implicitHeight, sheet.maximumHeight - sheet.topPadding
                             - sheet.bottomPadding - sheet.headerHeight - parent.spacing)
            contentHeight: columns.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Flow {
                id: columns
                width: parent.width
                spacing: sheet.scaledSize(28)

                Repeater {
                    model: sheet.filteredGroups

                    delegate: Column {
                        id: group
                        required property var modelData
                        width: (columns.width - columns.spacing) / 2
                        spacing: sheet.scaledSize(6)

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

                            delegate: Item {
                                id: shortcut
                                required property var modelData
                                width: group.width
                                height: sheet.scaledSize(24)

                                Label {
                                    anchors.left: parent.left
                                    anchors.right: keys.left
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: shortcut.modelData[0]
                                    elide: Text.ElideRight
                                    color: sheet.textColor
                                    font.family: "IBM Plex Mono"
                                    font.pixelSize: sheet.scaledSize(12)
                                }

                                Rectangle {
                                    id: keys
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: keyLabel.implicitWidth + sheet.scaledSize(12)
                                    height: sheet.scaledSize(20)
                                    radius: 4
                                    color: Qt.rgba(0.5, 0.5, 0.5, 0.16)

                                    Label {
                                        id: keyLabel
                                        anchors.centerIn: parent
                                        text: sheet.macOS ? shortcut.modelData[1] : shortcut.modelData[2]
                                        color: sheet.textColor
                                        font.family: "IBM Plex Mono"
                                        font.pixelSize: sheet.scaledSize(12)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
