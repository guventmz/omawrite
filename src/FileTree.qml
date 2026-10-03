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
    property string keyPrefix: ""
    property var marked: []
    property var markTarget: null
    property bool searching: false
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

    // Rebuilding the rows destroys the one holding focus, so focus is handed
    // back to the list a turn later, once the new rows exist.
    function refresh() {
        if (searching) {
            rows = vault.findEntries(searchField.text);
            list.currentIndex = rows.length > 0 ? 0 : -1;
            return;
        }
        var hadFocus = list.activeFocus || renamingPath !== "";
        var selected = currentRow() ? currentRow().path : "";
        rows = vault.tree();
        select(selected);
        if (hadFocus && renamingPath === "")
            Qt.callLater(focusList);
    }

    // "/" searches the whole vault; Enter jumps to the match in the tree.
    function startSearch() {
        searching = true;
        searchField.text = "";
        rows = [];
        searchField.forceActiveFocus();
    }

    function finishSearch(jump) {
        var row = jump ? currentRow() : null;
        searching = false;
        if (row)
            vault.revealPath(row.path);
        refresh();
        if (row)
            select(row.path);
        Qt.callLater(focusList);
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
        if (searching || renamingPath !== "")
            return;
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
            Qt.callLater(focusList);
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
            Qt.callLater(focusList);
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

    // Netrw marking: mf marks notes, mt picks the target folder, mm moves.
    function toggleMark() {
        var row = currentRow();
        if (!row)
            return;
        var index = marked.indexOf(row.path);
        marked = index < 0 ? marked.concat([row.path])
                           : marked.filter(function(path) { return path !== row.path; });
        list.incrementCurrentIndex();
    }

    function setMarkTarget() {
        var row = currentRow();
        markTarget = !row ? "" : (row.folder ? row.path : parentOf(row.path));
        message("Target: " + (markTarget === "" ? vault.name : markTarget));
    }

    function moveMarked() {
        if (marked.length === 0) {
            message("Nothing marked (mf)");
            return;
        }
        if (markTarget === null) {
            message("No target folder (mt)");
            return;
        }
        var paths = marked;
        marked = [];
        for (var i = 0; i < paths.length; ++i)
            moveTo(paths[i], markTarget);
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
        visible: !tree.searching
        text: vault.ready ? vault.name : "No vault folder"
        elide: Text.ElideRight
        color: tree.mutedColor
        font.family: "IBM Plex Mono"
        font.pixelSize: tree.scaledSize(11)
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1
    }

    Item {
        anchors.fill: header
        visible: tree.searching

        Label {
            id: slash
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "/"
            color: tree.highlightColor
            font.family: "IBM Plex Mono"
            font.pixelSize: tree.scaledSize(13)
        }

        TextInput {
            id: searchField
            objectName: "fileTreeSearch"
            anchors.left: slash.right
            anchors.leftMargin: 4
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            color: tree.textColor
            selectionColor: tree.highlightColor
            font.family: "IBM Plex Mono"
            font.pixelSize: tree.scaledSize(13)
            clip: true
            onTextChanged: {
                if (tree.searching)
                    tree.refresh();
            }
            Keys.onPressed: function(event) {
                var control = tree.macOS ? (event.modifiers & Qt.MetaModifier) : (event.modifiers & Qt.ControlModifier);
                if (event.key === Qt.Key_Down || (control && event.key === Qt.Key_N)) {
                    list.incrementCurrentIndex();
                } else if (event.key === Qt.Key_Up || (control && event.key === Qt.Key_P)) {
                    list.decrementCurrentIndex();
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    tree.finishSearch(true);
                } else if (event.key === Qt.Key_Escape) {
                    tree.finishSearch(false);
                } else {
                    return;
                }
                event.accepted = true;
            }
        }
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

        // Netrw's keys, with arrows and the original ones alongside:
        //   j k / C-n C-p  move        h l / Left Right  fold / unfold
        //   Enter o v t    open        -                 up to the parent folder
        //   % N            new note    d Shift+N         new folder
        //   R r F2         rename      D Cmd+Backspace   move to the Trash
        //   mf mt mm mu    mark, target, move marked, unmark      M  move via picker
        //   gg G           top, bottom C-l               refresh
        Keys.onPressed: function(event) {
            var row = tree.currentRow();
            var text = event.text;
            var control = tree.macOS ? (event.modifiers & Qt.MetaModifier) : (event.modifiers & Qt.ControlModifier);
            var command = tree.macOS ? (event.modifiers & Qt.ControlModifier) : false;
            var prefix = tree.keyPrefix;
            tree.keyPrefix = "";

            if (prefix === "m") {
                if (text === "f")
                    tree.toggleMark();
                else if (text === "t")
                    tree.setMarkTarget();
                else if (text === "m")
                    tree.moveMarked();
                else if (text === "u")
                    tree.marked = [];
            } else if (prefix === "g") {
                if (text === "g")
                    currentIndex = 0;
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                       || text === "o" || text === "v" || text === "t") {
                tree.activate(currentIndex);
            } else if (text === "j" || (control && event.key === Qt.Key_N)) {
                incrementCurrentIndex();
            } else if (text === "k" || (control && event.key === Qt.Key_P)) {
                decrementCurrentIndex();
            } else if ((text === "l" || event.key === Qt.Key_Right) && row && row.folder) {
                if (!row.expanded)
                    vault.setExpanded(row.path, true);
                else if (currentIndex + 1 < count)
                    currentIndex += 1;
            } else if ((text === "h" || event.key === Qt.Key_Left) && row) {
                if (row.folder && row.expanded)
                    vault.setExpanded(row.path, false);
                else if (tree.parentOf(row.path) !== "")
                    tree.select(tree.parentOf(row.path));
            } else if (text === "-" && row) {
                var parent = tree.parentOf(row.path);
                if (parent !== "") {
                    tree.select(parent);
                    vault.setExpanded(parent, false);
                }
            } else if (text === "%" || text === "n") {
                tree.createNote();
            } else if (text === "d" || text === "N") {
                tree.createFolder();
            } else if (text === "R" || text === "r" || event.key === Qt.Key_F2) {
                if (row)
                    tree.startRename(row.path);
            } else if (text === "D" || event.key === Qt.Key_Delete
                       || ((command || control) && event.key === Qt.Key_Backspace)) {
                tree.trashCurrent();
            } else if (text === "M") {
                if (row)
                    tree.moveRequested(row.path, row.name);
            } else if (text === "/") {
                tree.startSearch();
            } else if (text === "m" || text === "g") {
                tree.keyPrefix = text;
            } else if (text === "G") {
                currentIndex = count - 1;
            } else if (control && event.key === Qt.Key_L) {
                tree.refresh();
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

            Rectangle {
                x: 3
                anchors.verticalCenter: parent.verticalCenter
                width: tree.scaledSize(5)
                height: width
                radius: width / 2
                color: tree.highlightColor
                visible: tree.marked.indexOf(row.modelData.path) >= 0
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
                      + (row.modelData.folder && tree.markTarget === row.modelData.path ? "  ← target" : "")
                      + (tree.searching && row.modelData.location ? "   " + row.modelData.location : "")
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
                // A hidden rename field must not keep focus, or every key
                // after a rename would vanish into it.
                onVisibleChanged: {
                    if (visible) {
                        text = row.modelData.name;
                        selectAll();
                        forceActiveFocus();
                    } else {
                        focus = false;
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
        opacity: list.activeFocus || tree.searching ? 1 : 0
        text: vault.ready
            ? (tree.searching ? "↵ jump to it   esc back"
                              : "j k move  ↵ open  - up  / search  % note  d folder\nR rename  D trash  M move  mf mt mm marks")
            : "Choose a vault: " + (tree.macOS ? "⇧⌘O" : "Ctrl+Shift+O")
        color: tree.mutedColor
        font.family: "IBM Plex Mono"
        font.pixelSize: tree.scaledSize(10)
        lineHeight: 1.3
    }
}
