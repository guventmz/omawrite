import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// One keyboard-driven list for every way of reaching a note:
//   "open"      fuzzy-find a note by title, or create it
//   "text"      search the text of every note
//   "backlinks" notes that link to the current one
//   "link"      pick a note to insert as a [[link]]
Popup {
    id: picker

    property string mode: "open"
    property url currentNoteUrl
    property string currentNoteTitle
    property string movingPath
    property string movingName
    property string fontFamily: "IBM Plex Mono"
    property bool darkMode: true
    property real textScale: 1
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"
    property color highlightColor: "#428bca"
    property real maximumHeight: 480
    property color surfaceColor: "#22221f"

    property var results: []
    property var allBacklinks: []

    signal noteChosen(url noteUrl, int line)
    signal createRequested(string title)
    signal linkChosen(string title)
    signal chooseVaultRequested()
    signal folderChosen(string movingPath, string folder)

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    function show(newMode) {
        mode = newMode;
        open();
    }

    readonly property string placeholder: !vault.ready ? "No vault folder yet"
        : mode === "text" ? "Search all notes"
        : mode === "backlinks" ? "Notes linking to " + currentNoteTitle
        : mode === "link" ? "Link to note"
        : mode === "move" ? "Move “" + movingName + "” to folder"
        : "Open or create a note"

    function refreshResults() {
        var query = filterField.text.trim();
        if (!vault.ready) {
            results = [{ action: "vault", title: "Choose a vault folder…", folder: "" }];
        } else if (mode === "move") {
            results = vault.findFolders(query, 50);
        } else if (mode === "text") {
            results = query.length < 2 ? [] : vault.searchText(query, 200);
        } else if (mode === "backlinks") {
            var needle = query.toLocaleLowerCase();
            results = needle.length === 0 ? allBacklinks : allBacklinks.filter(function(entry) {
                return (entry.title + " " + entry.snippet).toLocaleLowerCase().indexOf(needle) !== -1;
            });
        } else {
            var notes = vault.findNotes(query, 50);
            if (mode === "link") {
                notes = notes.filter(function(note) {
                    return !backend.sameFile(note.url, currentNoteUrl);
                });
            }
            var exact = notes.some(function(note) {
                return note.title.toLocaleLowerCase() === query.toLocaleLowerCase();
            });
            if (query.length > 0 && !exact)
                notes.push({ action: "create", title: query, folder: "" });
            results = notes;
        }
        resultList.currentIndex = 0;
    }

    function choose(index) {
        var entry = results[index];
        if (!entry)
            return;
        close();
        if (entry.action === "vault")
            chooseVaultRequested();
        else if (mode === "link")
            linkChosen(entry.title);
        else if (mode === "move")
            folderChosen(movingPath, entry.path);
        else if (entry.action === "create")
            createRequested(entry.title);
        else
            noteChosen(entry.url, entry.line === undefined ? -1 : entry.line);
    }

    readonly property bool twoLineRows: mode === "text" || mode === "backlinks"
    readonly property real rowHeight: scaledSize(twoLineRows ? 54 : 36)
    readonly property real headerHeight: scaledSize(40) + 13

    height: Math.min(maximumHeight, topPadding + bottomPadding + headerHeight
                     + Math.max(scaledSize(36), resultList.contentHeight))

    modal: false
    focus: true
    padding: 8
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.elevation: 8

    onAboutToShow: {
        filterField.text = "";
        if (vault.ready)
            vault.refresh();
        allBacklinks = mode === "backlinks" ? vault.backlinks(currentNoteUrl) : [];
        refreshResults();
        filterField.forceActiveFocus();
    }

    Timer {
        id: textSearchDelay
        interval: 120
        onTriggered: picker.refreshResults()
    }

    background: Rectangle {
        radius: 9
        color: picker.surfaceColor
    }

    contentItem: Column {
        spacing: 6

        Item {
            width: parent.width
            height: picker.scaledSize(40)

            TextInput {
                id: filterField
                objectName: "notePickerField"
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: modeLabel.width + 16
                verticalAlignment: TextInput.AlignVCenter
                color: picker.textColor
                selectByMouse: true
                clip: true
                font.family: picker.fontFamily
                font.pixelSize: picker.scaledSize(17)
                onTextChanged: picker.mode === "text" ? textSearchDelay.restart()
                                                      : picker.refreshResults()

                Keys.onPressed: function(event) {
                    var control = Qt.platform.os === "osx" ? (event.modifiers & Qt.MetaModifier)
                                                           : (event.modifiers & Qt.ControlModifier);
                    if (control && event.key === Qt.Key_N) {
                        resultList.incrementCurrentIndex();
                        event.accepted = true;
                    } else if (control && event.key === Qt.Key_P) {
                        resultList.decrementCurrentIndex();
                        event.accepted = true;
                    }
                }
                Keys.onUpPressed: resultList.decrementCurrentIndex()
                Keys.onDownPressed: resultList.incrementCurrentIndex()
                Keys.onBacktabPressed: resultList.decrementCurrentIndex()
                Keys.onTabPressed: resultList.incrementCurrentIndex()
                Keys.onReturnPressed: picker.choose(resultList.currentIndex)
                Keys.onEnterPressed: picker.choose(resultList.currentIndex)
            }

            Label {
                anchors.left: filterField.left
                anchors.right: filterField.right
                anchors.verticalCenter: parent.verticalCenter
                text: picker.placeholder
                elide: Text.ElideRight
                visible: filterField.text.length === 0
                color: picker.mutedColor
                font: filterField.font
            }

            Label {
                id: modeLabel
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: vault.name
                color: picker.mutedColor
                font.family: picker.fontFamily
                font.pixelSize: picker.scaledSize(11)
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: picker.mutedColor
            opacity: 0.3
        }

        ListView {
            id: resultList
            objectName: "notePickerList"
            width: parent.width
            height: picker.availableHeight - picker.headerHeight
            clip: true
            model: picker.results
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            footer: Label {
                width: resultList.width
                height: visible ? picker.scaledSize(36) : 0
                visible: resultList.count === 0
                leftPadding: 8
                verticalAlignment: Text.AlignVCenter
                text: picker.mode === "backlinks" ? "No notes link here yet"
                    : picker.mode === "text" && filterField.text.trim().length >= 2 ? "No matches"
                    : picker.mode === "text" ? "Type to search"
                    : "No notes yet — type a title to create one"
                color: picker.mutedColor
                font.family: picker.fontFamily
                font.pixelSize: picker.scaledSize(13)
            }

            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index

                width: resultList.width
                height: picker.rowHeight
                leftPadding: 8
                rightPadding: 8
                highlighted: ListView.isCurrentItem
                onClicked: picker.choose(index)

                background: Rectangle {
                    radius: 5
                    color: picker.highlightColor
                    opacity: row.highlighted ? 0.22 : (row.hovered ? 0.12 : 0)
                }

                contentItem: Item {
                    Label {
                        id: titleLabel
                        anchors.left: parent.left
                        anchors.right: folderLabel.left
                        anchors.rightMargin: 8
                        anchors.top: picker.twoLineRows ? parent.top : undefined
                        anchors.topMargin: picker.scaledSize(6)
                        anchors.verticalCenter: picker.twoLineRows ? undefined : parent.verticalCenter
                        text: row.modelData.action === "create"
                            ? (picker.mode === "link" ? "Link to new note “" : "Create “")
                              + row.modelData.title + "”"
                            : row.modelData.title
                        elide: Text.ElideRight
                        color: row.modelData.action ? picker.highlightColor : picker.textColor
                        font.family: picker.fontFamily
                        font.pixelSize: picker.scaledSize(15)
                    }

                    Label {
                        id: folderLabel
                        anchors.right: parent.right
                        anchors.verticalCenter: titleLabel.verticalCenter
                        text: row.modelData.folder || ""
                        color: picker.mutedColor
                        font.family: picker.fontFamily
                        font.pixelSize: picker.scaledSize(11)
                    }

                    Label {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: titleLabel.bottom
                        anchors.topMargin: picker.scaledSize(3)
                        visible: picker.twoLineRows
                        text: row.modelData.snippet || ""
                        elide: Text.ElideRight
                        color: picker.mutedColor
                        font.family: picker.fontFamily
                        font.pixelSize: picker.scaledSize(12)
                    }
                }
            }
        }
    }
}
