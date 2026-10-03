import QtQuick
import QtQuick.Controls

// The vault's folders and notes. Everything works from the keyboard:
// arrows move and fold, Enter opens, N / Shift+N create, R renames,
// M moves, Cmd+Backspace trashes, Escape goes back to the editor.
FocusScope {
    id: tree

    property url currentNoteUrl
    property bool macOS: false
    property real textScale: 1
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"
    property color highlightColor: "#428bca"
    property color surfaceColor: "#22221f"

    property var rows: []
    property string renamingPath: ""
    property string openAfterRename: ""
    property var dragging: null
    property string dropTarget: noDropTarget
    readonly property string noDropTarget: "\u0000"

    signal noteChosen(url noteUrl)
    signal newNoteReady(url noteUrl)
    signal moveRequested(string path, string name)
    signal entryMoved(url from, url to)
    signal entryTrashed(url entryUrl)
    signal beforeChange()
    signal message(string text)
    signal linksRewritten()
    signal done()

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    function parentOf(path) {
        var slash = path.lastIndexOf("/");
        return slash < 0 ? "" : path.slice(0, slash);
    }

    function currentRow() {
        return list.currentIndex >= 0 && list.currentIndex < rows.length ? rows[list.currentIndex] : null;
    }

    // New entries go next to the selection: into it when it is a folder.
    function currentFolder() {
        var row = currentRow();
        return !row ? "" : (row.folder ? row.path : parentOf(row.path));
    }

    function refresh() {
        var selected = currentRow() ? currentRow().path : "";
        rows = vault.tree();
        select(selected);
    }

    function select(path) {
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].path === path) {
                list.currentIndex = i;
                list.positionViewAtIndex(i, ListView.Contain);
                return;
            }
        }
        if (list.currentIndex >= rows.length)
            list.currentIndex = rows.length - 1;
    }

    function revealCurrentNote() {
        vault.reveal(currentNoteUrl);
        refresh();
        select(vault.relativePath(currentNoteUrl));
    }

    function focusList() {
        list.forceActiveFocus();
        if (list.currentIndex < 0 && rows.length > 0)
            list.currentIndex = 0;
    }

    function activate(index) {
        var row = rows[index];
        if (!row)
            return;
        list.currentIndex = index;
        if (row.folder)
            vault.setExpanded(row.path, !row.expanded);
        else
            noteChosen(vault.urlFor(row.path));
    }

    function startRename(path) {
        select(path);
        renamingPath = path;
    }

    function commitRename(path, name) {
        renamingPath = "";
        beforeChange();
        var from = vault.urlFor(path);
        var result = vault.rename(path, name);
        if (result.error !== undefined) {
            message(result.error);
        } else {
            entryMoved(from, vault.urlFor(result.path));
            select(result.path);
            if (result.updatedNotes > 0) {
                linksRewritten();
                message("Updated links in " + result.updatedNotes
                        + (result.updatedNotes === 1 ? " note" : " notes"));
            }
        }
        var opening = result.error === undefined && openAfterRename === path;
        openAfterRename = "";
        if (opening)
            openNewNoteLater(vault.urlFor(result.path));
        else
            focusList();
    }

    // A new note opens ready to write, with focus in the editor. It waits a
    // turn: the rename field is being torn down while its Enter key is still
    // being handled, and focus would otherwise fall back into the tree.
    function openNewNoteLater(noteUrl) {
        Qt.callLater(function() { tree.newNoteReady(noteUrl); });
    }

    function cancelRename() {
        var opening = openAfterRename !== "" && openAfterRename === renamingPath ? openAfterRename : "";
        renamingPath = "";
        openAfterRename = "";
        if (opening !== "")
            openNewNoteLater(vault.urlFor(opening));
        else
            focusList();
    }

    function createNote() {
        var path = vault.createNoteIn(currentFolder());
        if (path === "")
            return;
        openAfterRename = path;
        startRename(path);
    }

    function createFolder() {
        var path = vault.createFolder(currentFolder());
        if (path !== "")
            startRename(path);
    }

    function moveTo(path, folder) {
        beforeChange();
        var from = vault.urlFor(path);
        var result = vault.move(path, folder);
        if (result.error !== undefined) {
            message(result.error);
            return;
        }
        entryMoved(from, vault.urlFor(result.path));
        select(result.path);
    }

    function trashCurrent() {
        var row = currentRow();
        if (!row)
            return;
        var entryUrl = vault.urlFor(row.path);
        var index = list.currentIndex;
        if (vault.trash(row.path)) {
            entryTrashed(entryUrl);
            message("Moved " + row.name + " to the Trash");
            list.currentIndex = Math.min(index, rows.length - 1);
        } else {
            message("Could not move " + row.name + " to the Trash");
        }
    }

    function dragMoved(scenePoint) {
        var point = list.mapFromItem(null, scenePoint.x, scenePoint.y);
        dragLabel.x = point.x + 12;
        dragLabel.y = point.y - dragLabel.height / 2;
        var index = list.indexAt(point.x, point.y + list.contentY);
        var row = rows[index];
        dropTarget = index < 0 ? "" : (row.folder ? row.path : parentOf(row.path));
    }

    function finishDrag() {
        if (dragging && dropTarget !== noDropTarget && dropTarget !== parentOf(dragging.path))
            moveTo(dragging.path, dropTarget);
        dragging = null;
        dropTarget = noDropTarget;
    }

    Connections {
        target: vault
        function onTreeChanged() { tree.refresh(); }
        function onPathChanged() { tree.refresh(); }
    }

    Component.onCompleted: refresh()

    Label {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        text: vault.ready ? vault.name : "No vault folder"
        elide: Text.ElideRight
        color: tree.mutedColor
        font.family: "IBM Plex Mono"
        font.pixelSize: tree.scaledSize(11)
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1
    }

    ListView {
        id: list
        objectName: "fileTreeList"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: hints.top
        anchors.margins: 6
        clip: true
        focus: true
        model: tree.rows
        highlightMoveDuration: 0
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Keys.onPressed: function(event) {
            var row = tree.currentRow();
            var command = event.modifiers & Qt.ControlModifier;
            var plain = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier));
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                tree.activate(currentIndex);
            } else if (event.key === Qt.Key_Right && row && row.folder) {
                if (!row.expanded)
                    vault.setExpanded(row.path, true);
                else if (currentIndex + 1 < count)
                    currentIndex += 1;
            } else if (event.key === Qt.Key_Left && row) {
                if (row.folder && row.expanded)
                    vault.setExpanded(row.path, false);
                else if (tree.parentOf(row.path) !== "")
                    tree.select(tree.parentOf(row.path));
            } else if (plain && event.key === Qt.Key_N) {
                if (event.modifiers & Qt.ShiftModifier)
                    tree.createFolder();
                else
                    tree.createNote();
            } else if ((plain && event.key === Qt.Key_R) || event.key === Qt.Key_F2) {
                if (row)
                    tree.startRename(row.path);
            } else if (plain && event.key === Qt.Key_M) {
                if (row)
                    tree.moveRequested(row.path, row.name);
            } else if ((command && event.key === Qt.Key_Backspace) || event.key === Qt.Key_Delete) {
                tree.trashCurrent();
            } else if (event.key === Qt.Key_Escape) {
                tree.done();
            } else {
                return;
            }
            event.accepted = true;
        }

        delegate: Item {
            id: row
            required property var modelData
            required property int index

            readonly property bool isCurrentNote: !modelData.folder
                && backend.sameFile(vault.urlFor(modelData.path), tree.currentNoteUrl)
            readonly property bool isDropTarget: tree.dragging !== null && modelData.folder
                && tree.dropTarget === modelData.path
            readonly property real indent: 8 + modelData.depth * tree.scaledSize(14)

            width: list.width
            height: tree.scaledSize(28)

            Rectangle {
                anchors.fill: parent
                radius: 5
                color: tree.highlightColor
                opacity: row.isDropTarget ? 0.35
                    : row.ListView.isCurrentItem && list.activeFocus ? 0.22
                    : row.ListView.isCurrentItem || row.isCurrentNote ? 0.1
                    : rowHover.hovered ? 0.06 : 0
            }

            Label {
                id: chevron
                x: row.indent
                anchors.verticalCenter: parent.verticalCenter
                width: tree.scaledSize(14)
                text: row.modelData.folder ? (row.modelData.expanded ? "▾" : "▸") : ""
                color: tree.mutedColor
                font.pixelSize: tree.scaledSize(11)
            }

            Label {
                anchors.left: chevron.right
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: tree.renamingPath !== row.modelData.path
                text: row.modelData.name
                elide: Text.ElideRight
                color: row.isCurrentNote ? tree.highlightColor : tree.textColor
                font.family: "IBM Plex Mono"
                font.pixelSize: tree.scaledSize(13)
                font.bold: row.modelData.folder
            }

            TextInput {
                id: renameField
                anchors.left: chevron.right
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: tree.renamingPath === row.modelData.path
                color: tree.textColor
                selectionColor: tree.highlightColor
                selectByMouse: true
                clip: true
                font.family: "IBM Plex Mono"
                font.pixelSize: tree.scaledSize(13)
                onVisibleChanged: {
                    if (visible) {
                        text = row.modelData.name;
                        selectAll();
                        forceActiveFocus();
                    }
                }
                Keys.onReturnPressed: tree.commitRename(row.modelData.path, text)
                Keys.onEnterPressed: tree.commitRename(row.modelData.path, text)
                Keys.onEscapePressed: tree.cancelRename()
                onActiveFocusChanged: {
                    if (!activeFocus && visible)
                        tree.commitRename(row.modelData.path, text);
                }
            }

            HoverHandler {
                id: rowHover
            }

            TapHandler {
                onTapped: {
                    tree.focusList();
                    tree.activate(row.index);
                }
            }

            DragHandler {
                target: null
                enabled: tree.renamingPath === ""
                onActiveChanged: {
                    if (active)
                        tree.dragging = row.modelData;
                    else
                        tree.finishDrag();
                }
                onCentroidChanged: {
                    if (active)
                        tree.dragMoved(centroid.scenePosition);
                }
            }
        }
    }

    Label {
        id: dragLabel
        visible: tree.dragging !== null
        text: tree.dragging ? tree.dragging.name : ""
        color: tree.textColor
        font.family: "IBM Plex Mono"
        font.pixelSize: tree.scaledSize(13)
        padding: 4
        background: Rectangle {
            radius: 4
            color: tree.highlightColor
            opacity: 0.3
        }
    }

    Label {
        id: hints
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        visible: true
        opacity: list.activeFocus ? 1 : 0
        text: vault.ready
            ? "↵ open  N note  ⇧N folder\nR rename  M move  " + (tree.macOS ? "⌘⌫" : "Del") + " trash"
            : "Choose a vault: " + (tree.macOS ? "⇧⌘O" : "Ctrl+Shift+O")
        color: tree.mutedColor
        font.family: "IBM Plex Mono"
        font.pixelSize: tree.scaledSize(10)
        lineHeight: 1.3
    }
}
