import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import QtQuick.Window
import "EditorMutations.js" as EditorMutations

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: (backend.modified ? "* " : "") + backend.fileName + " - Omawrite"

    readonly property bool darkMode: backend.darkMode
    // Qt maps "Ctrl" to Command on macOS, so most shortcuts carry over as-is;
    // only the ones macOS reserves or spells differently need their own keys.
    readonly property bool macOS: Qt.platform.os === "osx"
    readonly property color pageColor: backend.themeBackground
    readonly property color textColor: backend.themeForeground
    readonly property color strongTextColor: backend.themeForeground
    readonly property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    readonly property color selectionFill: backend.themeSelection
    // The desktop's text size knob (GNOME's text-scaling-factor, which
    // `omarchy display text size` drives) anchored so its 12px default leaves
    // the app at the sizes it was designed around.
    // Two knobs on top of it: textZoom sizes the writing, interfaceZoom
    // everything around it.
    readonly property real textScale: backend.textScale * backend.interfaceZoom
    readonly property int editorFontPixelSize:
        Math.max(8, Math.round(20 * backend.textScale * backend.textZoom))
    readonly property int footerHeight: scaledSize(32)
    // "column" wraps at a readable ~65 characters, "window" at the window
    // edge, and "off" lets long lines run on and scroll sideways.
    readonly property bool noWrap: backend.wrapMode === "off"
    readonly property int columnWidth: Math.min(
        Math.round(writerFontMetrics.averageCharacterWidth * 65),
        Math.max(360, width - Math.round(writerFontMetrics.averageCharacterWidth * 20)))
    readonly property int editorWidth: backend.wrapMode === "window"
        ? Math.max(360, width - 48 - Math.round(writerFontMetrics.averageCharacterWidth * 6))
        : columnWidth
    // Popups and bars sit a shade off the page, in whatever theme is active.
    readonly property color surfaceColor: Qt.tint(pageColor, darkMode ? "#1effffff" : "#0f000000")
    property bool closeConfirmed: false
    property bool searchOpen: false
    property bool searchUpdating: false
    property var searchMatches: []
    property int searchMatchIndex: -1
    property url pendingOpenUrl
    property string pendingAction: ""
    property bool replaceOpen: false
    property bool awaitingPendingSave: false
    property var backStack: []
    property var forwardStack: []
    property int backlinkCount: 0
    property var misspellings: []

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: backend.themeAccent
    color: pageColor

    onClosing: function(close) {
        if (closeConfirmed || !backend.modified)
            return;

        close.accepted = false;
        pendingAction = "close";
        if (!unsavedChangesDialog.opened)
            unsavedChangesDialog.open();
    }

    function requestOpen(url) {
        if (!backend.modified) {
            backend.open(url);
            return;
        }
        pendingOpenUrl = url;
        pendingAction = "open";
        unsavedChangesDialog.open();
    }

    // Jumping between notes saves the one being left; only an untitled draft
    // asks first, through the usual unsaved-changes dialog.
    function saveBeforeLeaving(url) {
        if (!backend.modified)
            return true;
        if (backend.fileUrl.toString() !== "") {
            backend.save();
            return !backend.modified;
        }
        requestOpen(url);
        return false;
    }

    function navigateTo(url, line) {
        if (url.toString() === "")
            return;
        if (!backend.sameFile(url, backend.fileUrl)) {
            if (!saveBeforeLeaving(url))
                return;
            var from = backend.fileUrl;
            backend.open(url);
            if (!backend.sameFile(backend.fileUrl, url))
                return;
            if (from.toString() !== "") {
                backStack = backStack.concat([from]);
                forwardStack = [];
            }
        }
        goToLine(line === undefined ? -1 : line);
    }

    // A fresh note opens with the caret under its heading, ready to write.
    function openNewNote(url) {
        navigateTo(url);
        if (backend.sameFile(backend.fileUrl, url))
            editor.cursorPosition = editor.text.length;
    }

    function goBack() {
        if (backStack.length === 0)
            return;
        var target = backStack[backStack.length - 1];
        var from = backend.fileUrl;
        if (!saveBeforeLeaving(target))
            return;
        backStack = backStack.slice(0, -1);
        backend.open(target);
        if (backend.sameFile(backend.fileUrl, target) && from.toString() !== "")
            forwardStack = forwardStack.concat([from]);
    }

    function goForward() {
        if (forwardStack.length === 0)
            return;
        var target = forwardStack[forwardStack.length - 1];
        var from = backend.fileUrl;
        if (!saveBeforeLeaving(target))
            return;
        forwardStack = forwardStack.slice(0, -1);
        backend.open(target);
        if (backend.sameFile(backend.fileUrl, target) && from.toString() !== "")
            backStack = backStack.concat([from]);
    }

    function goToLine(line) {
        var position = 0;
        for (var i = 0; i < line; ++i) {
            var next = editor.text.indexOf("\n", position);
            if (next < 0)
                break;
            position = next + 1;
        }
        editor.cursorPosition = position;
        editor.forceActiveFocus();
        Qt.callLater(editorFlick.ensureCursorVisible);
    }

    function followLinkAt(position) {
        var target = vault.linkAt(editor.text, position);
        if (target === "")
            return false;
        if (!vault.ready) {
            vaultFolderDialog.open();
            return true;
        }
        var url = vault.resolveLink(target);
        if (url.toString() === "")
            openNewNote(vault.createNote(target));
        else
            navigateTo(url);
        return true;
    }

    function openNotePicker(mode) {
        if (mode === "backlinks" && !vault.contains(backend.fileUrl))
            mode = "open";
        notePicker.show(mode);
    }

    function insertLinkToNote(title) {
        var position = editor.cursorPosition;
        var closed = editor.text.slice(position, position + 2) === "]]";
        EditorMutations.replaceRange(editor, position, position, closed ? title : title + "]]");
        if (closed)
            editor.cursorPosition = position + title.length + 2;
        editor.forceActiveFocus();
    }

    // Text replacements and spelling correction land when a word is finished,
    // just before the space, punctuation or newline that finishes it.
    function applyAutocorrection() {
        var fix = backend.autocorrection(editor.cursorPosition);
        if (fix.replacement !== undefined)
            EditorMutations.replaceRange(editor, fix.start, fix.end, fix.replacement);
    }

    function lookUp(word, position) {
        var rect = editor.positionToRectangle(position);
        var point = editor.mapToItem(null, rect.x, rect.y + rect.height * 0.78);
        backend.lookUp(word, point.x, point.y, editor.font.pixelSize);
    }

    function lookUpAtCursor() {
        var start = Math.min(editor.selectionStart, editor.selectionEnd);
        var end = Math.max(editor.selectionStart, editor.selectionEnd);
        if (start === end) {
            var hit = backend.wordAt(editor.text, editor.cursorPosition);
            start = hit.start;
            end = hit.end;
        }
        if (end > start)
            lookUp(editor.text.slice(start, end), start);
    }

    // Only the visible part of the page is checked, so long notes stay cheap.
    function refreshMisspellings() {
        if (!backend.spellCheckEnabled) {
            misspellings = [];
            return;
        }
        var top = Math.max(0, editorFlick.contentY - editor.y);
        misspellings = backend.misspellingsBetween(editor.positionAt(0, top),
                                                   editor.positionAt(editor.width, top + editorFlick.height));
    }

    function toggleFileTree() {
        if (fileTreePopup.opened)
            fileTreePopup.close();
        else
            fileTreePopup.open();
    }

    function entryMoved(from, to) {
        backend.fileMoved(from, to);
        backStack = backStack.map(function(url) { return backend.remapped(url, from, to); });
        forwardStack = forwardStack.map(function(url) { return backend.remapped(url, from, to); });
    }

    function entryTrashed(entryUrl) {
        if (backend.isAtOrUnder(backend.fileUrl, entryUrl))
            backend.closeDocument();
        var keep = function(url) { return !backend.isAtOrUnder(url, entryUrl); };
        backStack = backStack.filter(keep);
        forwardStack = forwardStack.filter(keep);
    }

    function refreshBacklinkCount() {
        if (!vault.contains(backend.fileUrl)) {
            backlinkCount = 0;
            return;
        }
        vault.refresh();
        backlinkCount = vault.backlinkCount(backend.fileUrl);
    }

    function completePendingAction() {
        var action = pendingAction;
        pendingAction = "";
        if (action === "close") {
            closeConfirmed = true;
            close();
        } else if (action === "open") {
            backend.open(pendingOpenUrl);
        }
    }

    FontMetrics {
        id: writerFontMetrics
        font.family: backend.editorFont
        font.pixelSize: win.editorFontPixelSize
    }

    // Every hardcoded size in the interface is expressed at text scale 1.
    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * win.textScale));
    }

    function toggleFullScreen() {
        win.visibility = win.visibility === Window.FullScreen
            ? Window.Windowed
            : Window.FullScreen;
    }

    function updateSearch() {
        var matches = [];
        var query = searchField.text;
        if (query.length > 0) {
            var haystack = editor.text.toLocaleLowerCase();
            var needle = query.toLocaleLowerCase();
            var position = 0;
            while ((position = haystack.indexOf(needle, position)) !== -1) {
                matches.push(position);
                position += Math.max(1, needle.length);
            }
        }
        searchMatches = matches;
        searchMatchIndex = matches.length > 0 ? 0 : -1;
        showSearchMatch();
    }

    function showSearchMatch() {
        var start = searchMatchIndex >= 0 ? searchMatches[searchMatchIndex] : -1;
        searchUpdating = true;
        backend.setSearchHighlight(searchField.text, start);
        if (start >= 0) {
            editor.select(start, start + searchField.text.length);
            editorFlick.ensureCursorVisible();
        }
        searchUpdating = false;
    }

    function moveSearch(direction) {
        if (searchMatches.length === 0)
            return;
        searchMatchIndex = (searchMatchIndex + direction + searchMatches.length)
                           % searchMatches.length;
        showSearchMatch();
    }

    function closeSearch() {
        searchOpen = false;
        searchUpdating = true;
        backend.setSearchHighlight("", -1);
        editor.deselect();
        searchUpdating = false;
        replaceOpen = false;
        editor.forceActiveFocus();
    }

    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        onActivated: backend.save()
    }

    Shortcut {
        // Command+H hides the app on macOS, so replace uses Option+Command+F there.
        sequence: win.macOS ? "Ctrl+Alt+F" : "Ctrl+H"
        context: Qt.ApplicationShortcut
        onActivated: {
            searchOpen = true;
            replaceOpen = true;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }

    Shortcut {
        sequence: "Ctrl+B"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("**", "**")
    }

    Shortcut {
        sequence: "Ctrl+I"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("*", "*")
    }

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.WindowShortcut
        onActivated: editor.insertLink()
    }

    Shortcut {
        // "?" is Shift+/ on most keyboards, so match it every way it arrives.
        sequences: ["Ctrl+?", "Ctrl+Shift+?", "Ctrl+Shift+/"]
        context: Qt.ApplicationShortcut
        onActivated: shortcutSheet.opened ? shortcutSheet.close() : shortcutSheet.open()
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        onActivated: backend.openDialog()
    }

    Shortcut {
        sequence: "Ctrl+N"
        context: Qt.ApplicationShortcut
        onActivated: backend.newWindow()
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        context: Qt.ApplicationShortcut
        onActivated: backend.saveAsDialog()
    }

    Shortcut {
        sequence: "Ctrl+Alt+P"
        context: Qt.ApplicationShortcut
        onActivated: backend.printDocument()
    }

    Shortcut {
        sequence: "Ctrl+Shift+P"
        context: Qt.ApplicationShortcut
        onActivated: backend.previewInBrowser()
    }

    Shortcut {
        sequence: "Ctrl+P"
        context: Qt.ApplicationShortcut
        onActivated: win.openNotePicker("open")
    }

    Shortcut {
        sequence: "Ctrl+Shift+F"
        context: Qt.ApplicationShortcut
        onActivated: win.openNotePicker("text")
    }

    Shortcut {
        sequence: "Ctrl+Shift+B"
        context: Qt.ApplicationShortcut
        onActivated: win.openNotePicker("backlinks")
    }

    Shortcut {
        sequence: "Ctrl+Shift+O"
        context: Qt.ApplicationShortcut
        onActivated: vaultFolderDialog.open()
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        context: Qt.WindowShortcut
        onActivated: win.followLinkAt(editor.cursorPosition)
    }

    Shortcut {
        sequences: win.macOS ? ["Ctrl+["] : ["Ctrl+[", "Alt+Left"]
        context: Qt.ApplicationShortcut
        onActivated: win.goBack()
    }

    Shortcut {
        sequences: win.macOS ? ["Ctrl+]"] : ["Ctrl+]", "Alt+Right"]
        context: Qt.ApplicationShortcut
        onActivated: win.goForward()
    }

    Shortcut {
        // On macOS "Meta" is the Control key, and Control+Command+F is the
        // system's own fullscreen chord.
        sequences: win.macOS ? ["Ctrl+Meta+F"] : ["Meta+F", "F11"]
        context: Qt.ApplicationShortcut
        onActivated: toggleFullScreen()
    }

    Shortcut {
        sequences: ["Ctrl+=", "Ctrl++"]
        context: Qt.ApplicationShortcut
        onActivated: backend.zoomText(1)
    }

    Shortcut {
        sequence: "Ctrl+-"
        context: Qt.ApplicationShortcut
        onActivated: backend.zoomText(-1)
    }

    Shortcut {
        sequence: "Ctrl+0"
        context: Qt.ApplicationShortcut
        onActivated: backend.zoomText(0)
    }

    Shortcut {
        sequences: ["Ctrl+Alt+=", "Ctrl+Alt++"]
        context: Qt.ApplicationShortcut
        onActivated: backend.zoomInterface(1)
    }

    Shortcut {
        sequence: "Ctrl+Alt+-"
        context: Qt.ApplicationShortcut
        onActivated: backend.zoomInterface(-1)
    }

    Shortcut {
        sequence: "Ctrl+Alt+0"
        context: Qt.ApplicationShortcut
        onActivated: backend.zoomInterface(0)
    }

    Shortcut {
        sequence: "Ctrl+Alt+T"
        context: Qt.ApplicationShortcut
        onActivated: themePicker.opened ? themePicker.close() : themePicker.open()
    }

    Shortcut {
        sequence: "Ctrl+Alt+L"
        context: Qt.ApplicationShortcut
        onActivated: backend.cycleWrapMode()
    }

    Shortcut {
        sequence: "Ctrl+,"
        context: Qt.ApplicationShortcut
        onActivated: shortcutSheet.opened ? shortcutSheet.close() : shortcutSheet.open()
    }

    Shortcut {
        sequence: "Ctrl+Shift+E"
        context: Qt.ApplicationShortcut
        onActivated: win.toggleFileTree()
    }

    Shortcut {
        sequence: "Ctrl+Meta+D"
        context: Qt.ApplicationShortcut
        enabled: win.macOS
        onActivated: win.lookUpAtCursor()
    }

    Shortcut {
        sequence: "Ctrl+W"
        context: Qt.ApplicationShortcut
        enabled: win.macOS
        onActivated: win.close()
    }

    Shortcut {
        sequence: "Ctrl+M"
        context: Qt.ApplicationShortcut
        enabled: win.macOS
        onActivated: win.showMinimized()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        onActivated: editor.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        context: Qt.WindowShortcut
        onActivated: editor.redo()
    }

    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.ApplicationShortcut
        onActivated: {
            searchOpen = true;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }

    Shortcut {
        sequence: "Ctrl+G"
        context: Qt.ApplicationShortcut
        enabled: win.searchOpen
        onActivated: win.moveSearch(1)
    }

    Connections {
        target: backend

        function onOpenDialogRequested() {
            openFileDialog.open();
        }

        function onSaveDialogRequested(suggestedUrl) {
            saveFileDialog.selectedFile = suggestedUrl;
            saveFileDialog.open();
        }

        function onCloseAfterSave() {
            win.closeConfirmed = true;
            win.close();
        }

        function onFileUrlChanged() {
            win.refreshBacklinkCount();
            spellingRefresh.restart();
        }

        function onSpellingChanged() {
            spellingRefresh.restart();
        }

        function onSaveSucceeded() {
            win.refreshBacklinkCount();
            win.awaitingPendingSave = false;
            if (win.pendingAction !== "")
                win.completePendingAction();
        }

        function onExternalChangeDetected(deleted, locallyModified) {
            externalChangeDialog.deleted = deleted;
            externalChangeDialog.locallyModified = locallyModified;
            externalChangeDialog.open();
        }
    }

    Dialogs.FileDialog {
        id: openFileDialog
        title: "Open File"
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: win.requestOpen(selectedFile)
    }

    Dialogs.FileDialog {
        id: saveFileDialog
        title: "Save File"
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: backend.saveAs(selectedFile)
        onRejected: {
            backend.fileDialogCanceled();
            win.awaitingPendingSave = false;
            win.pendingAction = "";
        }
    }

    Menu {
        id: editorMenu
        objectName: "editorMenu"
        popupType: Popup.Native

        property string word
        property int wordStart: -1
        property int wordEnd: -1
        property bool misspelled: false
        property var guesses: []

        // Like a native text view: right-clicking outside the selection
        // selects the word under the pointer first.
        function openAt(position) {
            var selectionStart = Math.min(editor.selectionStart, editor.selectionEnd);
            var selectionEnd = Math.max(editor.selectionStart, editor.selectionEnd);
            var inSelection = selectionStart !== selectionEnd
                && position >= selectionStart && position <= selectionEnd;
            if (inSelection) {
                wordStart = selectionStart;
                wordEnd = selectionEnd;
            } else {
                var hit = backend.wordAt(editor.text, position);
                wordStart = hit.start;
                wordEnd = hit.end;
                if (hit.end > hit.start)
                    editor.select(hit.start, hit.end);
                else
                    editor.cursorPosition = position;
            }
            word = editor.text.slice(wordStart, wordEnd);
            misspelled = !inSelection && backend.isMisspelled(word);
            guesses = misspelled ? backend.spellingGuesses(word) : [];
            editor.forceActiveFocus();
            popup();
        }

        Instantiator {
            model: editorMenu.guesses
            delegate: MenuItem {
                required property string modelData
                text: modelData
                onTriggered: EditorMutations.replaceRange(editor, editorMenu.wordStart,
                                                          editorMenu.wordEnd, modelData)
            }
            onObjectAdded: function(index, object) { editorMenu.insertItem(index, object); }
            onObjectRemoved: function(index, object) { editorMenu.removeItem(object); }
        }

        MenuItem {
            text: "No Guesses Found"
            enabled: false
            visible: editorMenu.misspelled && editorMenu.guesses.length === 0
            height: visible ? implicitHeight : 0
        }
        MenuSeparator {
            visible: editorMenu.misspelled
            height: visible ? implicitHeight : 0
        }
        MenuItem {
            text: "Learn Spelling"
            visible: editorMenu.misspelled
            height: visible ? implicitHeight : 0
            onTriggered: backend.learnWord(editorMenu.word)
        }
        MenuItem {
            text: "Ignore Spelling"
            visible: editorMenu.misspelled
            height: visible ? implicitHeight : 0
            onTriggered: backend.ignoreWord(editorMenu.word)
        }
        MenuSeparator {
            visible: editorMenu.misspelled
            height: visible ? implicitHeight : 0
        }
        MenuItem {
            text: "Look Up “" + (editorMenu.word.length > 24 ? editorMenu.word.slice(0, 24) + "…"
                                                                 : editorMenu.word) + "”"
            visible: win.macOS && editorMenu.word.trim().length > 0
            height: visible ? implicitHeight : 0
            onTriggered: win.lookUp(editorMenu.word, editorMenu.wordStart)
        }
        MenuSeparator {
            visible: win.macOS && editorMenu.word.trim().length > 0
            height: visible ? implicitHeight : 0
        }
        MenuItem {
            text: "Cut"
            enabled: editor.selectedText.length > 0
            onTriggered: editor.cut()
        }
        MenuItem {
            text: "Copy"
            enabled: editor.selectedText.length > 0
            onTriggered: editor.copy()
        }
        MenuItem {
            text: "Paste"
            onTriggered: {
                if (!editor.pasteClipboardUrlAsMarkdownLink())
                    editor.pasteClipboardAsPlainText();
            }
        }
        MenuSeparator {
            visible: backend.spellCheckAvailable
            height: visible ? implicitHeight : 0
        }
        MenuItem {
            text: "Check Spelling While Typing"
            checkable: true
            checked: backend.spellCheckEnabled
            visible: backend.spellCheckAvailable
            height: visible ? implicitHeight : 0
            onTriggered: backend.spellCheckEnabled = checked
        }
        MenuItem {
            text: "Correct Spelling Automatically"
            checkable: true
            checked: backend.autocorrectEnabled
            visible: backend.spellCheckAvailable
            enabled: backend.spellCheckEnabled
            height: visible ? implicitHeight : 0
            onTriggered: backend.autocorrectEnabled = checked
        }
        onClosed: editor.forceActiveFocus()
    }

    Dialogs.FolderDialog {
        id: vaultFolderDialog
        title: "Choose Vault Folder"
        onAccepted: {
            vault.setFolder(selectedFolder);
            win.refreshBacklinkCount();
            win.openNotePicker("open");
        }
    }

    NotePicker {
        id: notePicker
        objectName: "notePicker"
        x: Math.round((win.width - width) / 2)
        y: Math.round(win.height * 0.12)
        width: Math.min(win.scaledSize(680), win.width - 48)
        maximumHeight: Math.min(win.scaledSize(480), win.height * 0.7)
        currentNoteUrl: backend.fileUrl
        currentNoteTitle: backend.fileName.replace(/\.(md|markdown)$/i, "")
        darkMode: win.darkMode
        textScale: win.textScale
        textColor: win.textColor
        mutedColor: win.mutedColor
        highlightColor: backend.themeAccent
        surfaceColor: win.surfaceColor
        onNoteChosen: function(noteUrl, line) { win.navigateTo(noteUrl, line); }
        onCreateRequested: function(title) { win.openNewNote(vault.createNote(title)); }
        onLinkChosen: function(title) { win.insertLinkToNote(title); }
        onChooseVaultRequested: vaultFolderDialog.open()
        onFolderChosen: function(movingPath, folder) { fileTree.moveTo(movingPath, folder); }
        onClosed: mode === "move" && fileTreePopup.opened ? fileTree.focusList()
                                                          : editor.forceActiveFocus()
    }

    Popup {
        id: fileTreePopup
        objectName: "fileTreePopup"
        x: Math.round((win.width - width) / 2)
        y: Math.round(win.height * 0.1)
        width: Math.min(win.scaledSize(520), win.width - 48)
        height: Math.min(win.scaledSize(560), win.height * 0.8)
        padding: 0
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Material.elevation: 8

        background: Rectangle {
            radius: 9
            color: win.surfaceColor
        }

        onAboutToShow: fileTree.revealCurrentNote()
        onOpened: fileTree.focusList()
        onClosed: {
            if (!notePicker.opened)
                editor.forceActiveFocus();
        }

        contentItem:
FileTree {
                id: fileTree
                objectName: "fileTree"
                currentNoteUrl: backend.fileUrl
                macOS: win.macOS
                textScale: win.textScale
                textColor: win.textColor
                mutedColor: win.mutedColor
                highlightColor: backend.themeAccent
                surfaceColor: win.surfaceColor
                onNoteChosen: function(noteUrl) {
                    fileTreePopup.close();
                    win.navigateTo(noteUrl);
                }
                onNewNoteReady: function(noteUrl) {
                    fileTreePopup.close();
                    win.openNewNote(noteUrl);
                }
                onMoveRequested: function(path, name) {
                    notePicker.movingPath = path;
                    notePicker.movingName = name;
                    notePicker.show("move");
                }
                onEntryMoved: function(from, to) { win.entryMoved(from, to); }
                onEntryTrashed: function(entryUrl) { win.entryTrashed(entryUrl); }
                onBeforeChange: {
                    if (backend.modified && backend.fileUrl.toString() !== "")
                        backend.save();
                }
                onLinksRewritten: {
                    if (!backend.modified && vault.contains(backend.fileUrl))
                        backend.reloadFromDisk();
                }
                onMessage: function(text) { backend.showStatus(text); }
                onDone: fileTreePopup.close()
            }
    }

    ShortcutSheet {
        id: shortcutSheet
        objectName: "shortcutSheet"
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.scaledSize(860), win.width - 48)
        maximumHeight: win.height - 48
        macOS: win.macOS
        textScale: win.textScale
        textColor: win.textColor
        mutedColor: win.mutedColor
        headingColor: backend.themeForeground
        highlightColor: backend.themeAccent
        surfaceColor: win.surfaceColor
        onClosed: editor.forceActiveFocus()
    }

    UnsavedChangesDialog {
        id: unsavedChangesDialog
        fileName: backend.fileName
        darkMode: win.darkMode
        textScale: win.textScale
        textColor: win.textColor
        strongTextColor: win.strongTextColor
        activeButtonColor: backend.themeAccent
        containerWidth: win.width
        containerHeight: win.height

        onDiscardRequested: {
            backend.discardRecovery();
            win.completePendingAction();
        }

        onSaveRequested: {
            win.awaitingPendingSave = true;
            backend.save();
        }
        onCancelRequested: win.pendingAction = ""
    }

    ExternalChangeDialog {
        id: externalChangeDialog
        darkMode: win.darkMode
        textScale: win.textScale
        textColor: win.textColor
        strongTextColor: win.strongTextColor
        containerWidth: win.width
        containerHeight: win.height

        onKeepRequested: backend.keepExternalVersion()
        onReloadRequested: backend.reloadFromDisk()
    }


    Item {
        anchors.fill: parent

        Flickable {
            id: editorFlick
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            // Inset the scrolling area by the footer strip at both ends, so
            // scrolled text never runs under the footer buttons, status, or
            // word count, and stops just as far short of the top edge.
            anchors.topMargin: win.footerHeight
            anchors.bottomMargin: win.footerHeight
            clip: true
            contentWidth: win.noWrap ? Math.max(width, editor.x + editor.width + 24) : width
            contentHeight: Math.max(height, editor.y + editor.implicitHeight + 220)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                // Wheel scrolling moves contentY directly rather than
                // flicking the Flickable, so the bar has to be told about
                // that activity; linger briefly after the last event.
                active: hovered || pressed || wheelScroll.running || scrollLinger.running
            }

            Timer {
                id: scrollLinger
                interval: 600
            }

            // Flickable turns a wheel notch into a flick sized by the small
            // application font, which crawls next to a browser. Reproduce
            // Chromium's wheel physics instead (cc::ScrollOffsetAnimationCurve):
            // each notch moves 3 lines of 40px towards a running target, the
            // animation gets shorter as the outstanding distance grows, and a
            // notch landing mid-animation carries the current velocity into
            // the new curve, so sustained spinning keeps picking up speed.
            readonly property real wheelStep: win.scaledSize(120)

            FrameAnimation {
                id: wheelScroll
                running: false

                property real startY: 0
                property real targetY: 0
                property real duration: 0.2
                // Cubic bezier easing; ease-in-out (0.42, 0, 0.58, 1) for a
                // fresh scroll, with y1 tilted on retarget so the curve's
                // initial slope matches the velocity it inherits.
                property real cx1: 0.42
                property real cy1: 0
                readonly property real cx2: 0.58
                readonly property real cy2: 1

                onTriggered: {
                    var x = elapsedTime / duration;
                    if (x >= 1) {
                        editorFlick.contentY = editorFlick.snapToPixel(targetY);
                        stop();
                        return;
                    }
                    editorFlick.contentY = editorFlick.snapToPixel(
                        startY + (targetY - startY) * curveY(solveCurve(x)));
                }

                function begin(from, to, dur, slope) {
                    startY = from;
                    targetY = to;
                    duration = dur;
                    cx1 = 0.42;
                    cy1 = 0.42 * Math.max(-1000, Math.min(1000, slope));
                    restart();
                }

                function retarget(newTarget) {
                    var s = solveCurve(Math.min(1, elapsedTime / duration));
                    var pos = startY + (targetY - startY) * curveY(s);
                    var delta = newTarget - pos;
                    if (Math.abs(delta) < 0.5) {
                        editorFlick.contentY = newTarget;
                        stop();
                        return;
                    }

                    var velocity = curveDY(s) / Math.max(1e-6, curveDX(s))
                        * (targetY - startY) / duration;
                    var dur = editorFlick.wheelDuration(delta);
                    // When already moving faster than the eased curve would,
                    // bound the duration by the time to target at the current
                    // velocity; the 2.5x covers the ease-out tail.
                    if (velocity !== 0 && delta / velocity > 0)
                        dur = Math.min(dur, delta / velocity * 2.5);
                    begin(pos, newTarget, dur, velocity * dur / delta);
                }

                // Cubic bezier through (0,0), (cx1,cy1), (cx2,cy2), (1,1),
                // evaluated by Newton-solving the curve parameter from x.
                function curveX(s) { return 3 * s * (1 - s) * ((1 - s) * cx1 + s * cx2) + s * s * s; }
                function curveY(s) { return 3 * s * (1 - s) * ((1 - s) * cy1 + s * cy2) + s * s * s; }
                function curveDX(s) { return 3 * (1 - s) * (1 - s) * cx1 + 6 * (1 - s) * s * (cx2 - cx1) + 3 * s * s * (1 - cx2); }
                function curveDY(s) { return 3 * (1 - s) * (1 - s) * cy1 + 6 * (1 - s) * s * (cy2 - cy1) + 3 * s * s * (1 - cy2); }

                function solveCurve(x) {
                    var s = x;
                    for (var i = 0; i < 8; ++i) {
                        var error = curveX(s) - x;
                        if (Math.abs(error) < 0.001)
                            break;
                        var d = curveDX(s);
                        if (Math.abs(d) < 1e-6)
                            break;
                        s = Math.max(0, Math.min(1, s - error / d));
                    }
                    return s;
                }
            }

            WheelHandler {
                // Wayland compositors route every pointer's scroll through
                // one seat device that Qt classifies as a touchpad, so the
                // device type cannot tell a mouse wheel from two-finger
                // scrolling. Distinguish by event shape instead: discrete
                // wheel notches arrive with only angleDelta set, while
                // finger scrolling carries pixel-precise pixelDelta.
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(wheel) {
                    scrollLinger.restart();
                    var dx = wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x
                                                     : wheel.angleDelta.x / 120 * editorFlick.wheelStep;
                    if (dx !== 0 && editorFlick.contentWidth > editorFlick.width) {
                        editorFlick.contentX = Math.max(0, Math.min(editorFlick.contentWidth - editorFlick.width,
                                                                    editorFlick.contentX - dx));
                    }
                    if (wheel.pixelDelta.y !== 0)
                        editorFlick.scrollTo(editorFlick.clampContentY(editorFlick.contentY - wheel.pixelDelta.y));
                    else
                        editorFlick.scrollByWheel(wheel);
                    wheel.accepted = true;
                }
            }

            onMovementStarted: wheelScroll.stop()
            onContentYChanged: spellingRefresh.restart()
            onWidthChanged: spellingRefresh.restart()

            Timer {
                id: spellingRefresh
                interval: 250
                onTriggered: win.refreshMisspellings()
            }

            function scrollByWheel(wheel) {
                // High-resolution wheels report fractional notches; feed
                // those through the same animated path, like Chromium does
                // for every wheel-source event.
                var notches = wheel.angleDelta.y / 120;
                if (notches === 0)
                    return;

                if (wheelScroll.running) {
                    wheelScroll.retarget(clampContentY(wheelScroll.targetY - notches * wheelStep));
                    return;
                }

                var target = clampContentY(contentY - notches * wheelStep);
                if (target !== contentY)
                    wheelScroll.begin(contentY, target, wheelDuration(target - contentY), 0);
            }

            // Chromium's inverse-delta duration: 200ms for a single notch,
            // ramping down to 100ms once 480px are outstanding.
            function wheelDuration(delta) {
                var pixels = Math.abs(delta) / win.textScale;
                return Math.max(6, Math.min(12, 14 - pixels / 60)) / 60;
            }

            function clampContentY(y) {
                return Math.max(0, Math.min(Math.max(0, contentHeight - height), y));
            }

            // Whole device pixels keep natively hinted glyphs from
            // re-rasterizing mid-animation, which reads as shimmer.
            function snapToPixel(y) {
                return Math.round(y * Screen.devicePixelRatio) / Screen.devicePixelRatio;
            }

            // Jump to a position, abandoning any wheel animation still running.
            function scrollTo(y) {
                wheelScroll.stop();
                contentY = snapToPixel(y);
            }

            // Keep the editing caret within the viewport so writing past the
            // bottom edge scrolls the page along with the text.
            function ensureCursorVisible() {
                var margin = win.editorFontPixelSize * 2;
                var cursorTop = editor.y + editor.cursorRectangle.y;
                var cursorBottom = cursorTop + editor.cursorRectangle.height;
                var maxContentY = Math.max(0, contentHeight - height);

                if (cursorBottom + margin > contentY + height)
                    scrollTo(Math.min(maxContentY, cursorBottom + margin - height));
                else if (cursorTop - margin < contentY)
                    scrollTo(Math.max(0, cursorTop - margin));

                if (!win.noWrap)
                    return;
                var cursorLeft = editor.x + editor.cursorRectangle.x;
                var maxContentX = Math.max(0, contentWidth - width);
                if (cursorLeft + margin > contentX + width)
                    contentX = Math.min(maxContentX, cursorLeft + margin - width);
                else if (cursorLeft - margin < contentX)
                    contentX = Math.max(0, cursorLeft - margin);
            }

            TextEdit {
                id: editor
                objectName: "sourceEditor"
                x: win.noWrap ? Math.max(0, Math.round((editorFlick.width - win.columnWidth) / 2))
                              : Math.round((editorFlick.width - width) / 2)
                // The resting position counts from the window's top edge, so
                // take back the top inset the Flickable already adds.
                y: Math.max(0, Math.max(42, Math.round(win.height * 0.05)) - win.footerHeight)
                width: win.noWrap ? Math.max(win.columnWidth, implicitWidth + 2) : win.editorWidth
                height: Math.max(editorFlick.height - y - 96, implicitHeight + 20)
                text: ""
                textFormat: TextEdit.PlainText
                wrapMode: win.noWrap ? TextEdit.NoWrap : TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true
                activeFocusOnPress: true
                color: win.textColor
                selectedTextColor: win.strongTextColor
                selectionColor: win.selectionFill
                font.family: backend.editorFont
                font.pixelSize: win.editorFontPixelSize
                font.weight: Font.Normal
                // Native rendering hints glyphs to the pixel grid, which is
                // crispest at whole scale factors but misplaces and unevenly
                // rasterizes glyphs at fractional ones (and goes stale when
                // the compositor delivers the fractional scale after the
                // first frame). Fall back to Qt's scalable renderer there.
                renderType: Screen.devicePixelRatio % 1 === 0 ? TextEdit.NativeRendering : TextEdit.QtRendering
                cursorDelegate: Rectangle {
                    width: 1
                    color: win.strongTextColor
                }
                onCursorRectangleChanged: editorFlick.ensureCursorVisible()
                onCursorPositionChanged: {
                    backend.setCaretPosition(cursorPosition);
                    spellingRefresh.restart();
                }
                onFontChanged: {
                    backend.documentFontChanged();
                    spellingRefresh.restart();
                }

                function replaceSelectionWith(replacement) {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    EditorMutations.replaceRange(editor, start, end, replacement);
                }

                function wrapSelection(before, after) {
                    forceActiveFocus();
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    var selected = text.slice(start, end);
                    EditorMutations.replaceRange(editor, start, end,
                                                 before + selected + after,
                                                 before.length,
                                                 before.length + selected.length);
                }

                function insertLink() {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    var selected = text.slice(start, end);
                    var url = backend.clipboardUrl();
                    var label = selected.length > 0 ? selected : "link text";
                    var destination = url.length > 0 ? url : "https://";
                    var escapedLabel = escapeMarkdownLinkText(label);
                    var markdown = "[" + escapedLabel + "](" + escapeMarkdownLinkDestination(destination) + ")";
                    if (selected.length === 0) {
                        EditorMutations.replaceRange(editor, start, end, markdown,
                                                     1, 1 + escapedLabel.length);
                    } else if (url.length === 0) {
                        EditorMutations.replaceRange(editor, start, end, markdown,
                                                     escapedLabel.length + 3,
                                                     markdown.length - 1);
                    } else {
                        EditorMutations.replaceRange(editor, start, end, markdown);
                    }
                }

                function smartReturn(softBreak) {
                    if (softBreak) {
                        replaceSelectionWith("\n");
                        return;
                    }
                    var lineStart = text.lastIndexOf("\n", cursorPosition - 1) + 1;
                    var line = text.slice(lineStart, cursorPosition);
                    var before = text.slice(0, cursorPosition);
                    var fences = (before.match(/^\s*```/gm) || []).length;
                    if ((fences % 2) === 1) {
                        replaceSelectionWith("\n");
                        return;
                    }
                    var match = line.match(/^(\s*)([-+*]|\d+[.)]|>+)\s+(.*)$/);
                    if (match) {
                        if (match[3].length === 0) {
                            EditorMutations.replaceRange(editor, lineStart,
                                                         cursorPosition, "\n");
                        } else {
                            var marker = match[2];
                            if (/^\d/.test(marker))
                                marker = (parseInt(marker) + 1) + marker.slice(-1);
                            replaceSelectionWith("\n" + match[1] + marker + " ");
                        }
                        return;
                    }
                    replaceSelectionWith("\n\n");
                }

                function escapeMarkdownLinkText(linkText) {
                    return linkText.replace(/\\/g, "\\\\")
                                   .replace(/\[/g, "\\[")
                                   .replace(/\]/g, "\\]");
                }

                function escapeMarkdownLinkDestination(linkUrl) {
                    return linkUrl.replace(/\\/g, "\\\\")
                                  .replace(/\(/g, "\\(")
                                  .replace(/\)/g, "\\)");
                }

                function pasteClipboardUrlAsMarkdownLink() {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    if (start === end)
                        return false;

                    var url = backend.clipboardUrl();
                    if (url === "")
                        return false;

                    var selected = text.slice(start, end);
                    var leading = selected.match(/^\s*/)[0];
                    var trailing = selected.match(/\s*$/)[0];
                    var linkText = selected.slice(leading.length,
                                                  selected.length - trailing.length);
                    if (linkText === "")
                        return false;

                    replaceSelectionWith(leading + "[" + escapeMarkdownLinkText(linkText) + "]("
                                         + escapeMarkdownLinkDestination(url) + ")" + trailing);
                    return true;
                }

                function pasteClipboardAsPlainText() {
                    var pastedText = backend.clipboardText();
                    if (pastedText.length > 0)
                        replaceSelectionWith(pastedText);
                }

                function skipHiddenForward(position) {
                    var pos = position;
                    var ranges = backend.hiddenRangesAt(pos);
                    for (var i = 0; i < ranges.length; i++) {
                        if (pos >= ranges[i].start && pos < ranges[i].end) {
                            pos = ranges[i].end;
                            i = -1;
                        }
                    }
                    return pos;
                }

                function skipHiddenBackward(position) {
                    var pos = position;
                    var ranges = backend.hiddenRangesAt(pos);
                    for (var i = ranges.length - 1; i >= 0; i--) {
                        if (pos > ranges[i].start && pos <= ranges[i].end) {
                            pos = ranges[i].start;
                            i = ranges.length;
                        }
                    }
                    return pos;
                }

                function moveCursorVisibly(direction) {
                    if (selectionStart !== selectionEnd) {
                        cursorPosition = direction > 0
                            ? Math.max(selectionStart, selectionEnd)
                            : Math.min(selectionStart, selectionEnd);
                        return;
                    }

                    var pos = Math.max(0, Math.min(text.length, cursorPosition + direction));
                    cursorPosition = direction > 0
                        ? skipHiddenForward(pos)
                        : skipHiddenBackward(pos);
                }

                function movePage(direction, extendSelection) {
                    var pageStep = Math.max(win.editorFontPixelSize,
                                            editorFlick.height - win.editorFontPixelSize * 2);
                    var rect = cursorRectangle;
                    var targetY = rect.y + rect.height / 2 + direction * pageStep;
                    var target = positionAt(rect.x, Math.max(0, targetY));
                    if (extendSelection)
                        moveCursorSelection(target, TextEdit.SelectCharacters);
                    else
                        cursorPosition = target;
                }

                function deleteParagraphBreakBehindCursor() {
                    if (selectionStart !== selectionEnd || cursorPosition < 2)
                        return false;

                    if (text.slice(cursorPosition - 2, cursorPosition) !== "\n\n")
                        return false;

                    var start = cursorPosition - 2;
                    remove(start, cursorPosition);
                    cursorPosition = start;
                    return true;
                }

                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    var pasteKey = (event.key === Qt.Key_V)
                        && (event.modifiers & Qt.ControlModifier)
                        && !(event.modifiers & (Qt.AltModifier | Qt.MetaModifier | Qt.ShiftModifier));
                    var shiftInsert = (event.key === Qt.Key_Insert)
                        && (event.modifiers & Qt.ShiftModifier)
                        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier));
                    if (pasteKey || shiftInsert) {
                        if (!pasteClipboardUrlAsMarkdownLink())
                            pasteClipboardAsPlainText();
                        event.accepted = true;
                        return;
                    }

                    if (event.text === "[" && vault.ready && selectionStart === selectionEnd
                            && text.charAt(cursorPosition - 1) === "[") {
                        Qt.callLater(win.openNotePicker, "link");
                        return;
                    }

                    var returnKey = event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
                    var commandModifier = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier);
                    var wordBoundary = event.text === " " || /^[.,;:!?)]$/.test(event.text) || returnKey;
                    if (wordBoundary && !commandModifier && selectionStart === selectionEnd)
                        win.applyAutocorrection();
                    if (returnKey && !commandModifier) {
                        smartReturn(event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    } else if (!commandModifier && event.key === Qt.Key_Backspace
                               && deleteParagraphBreakBehindCursor()) {
                        event.accepted = true;
                    } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                               && event.key === Qt.Key_Right) {
                        moveCursorVisibly(1);
                        event.accepted = true;
                    } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                               && event.key === Qt.Key_Left) {
                        moveCursorVisibly(-1);
                        event.accepted = true;
                    } else if (!commandModifier
                               && (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp)) {
                        movePage(event.key === Qt.Key_PageDown ? 1 : -1,
                                 event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                }

                onTextChanged: {
                    spellingRefresh.restart();
                    if (win.searchUpdating)
                        return;
                    var contentChanged = backend.editorTextChanged();
                    if (win.searchOpen && contentChanged)
                        win.updateSearch();
                }

                // Qt Quick ignores underline colors, so misspellings get the
                // system's red dots drawn over the text instead.
                Repeater {
                    model: win.misspellings
                    delegate: Canvas {
                        required property var modelData
                        readonly property rect first: editor.positionToRectangle(modelData.start)
                        readonly property rect last: editor.positionToRectangle(modelData.end)
                        x: first.x
                        y: first.y + Math.round(editor.font.pixelSize * 1.18)
                        width: Math.max(4, (last.y > first.y ? editor.width : last.x) - first.x)
                        height: 3
                        onWidthChanged: requestPaint()
                        onPaint: {
                            var context = getContext("2d");
                            context.clearRect(0, 0, width, height);
                            context.fillStyle = win.darkMode ? "#ff6b6b" : "#e0352b";
                            for (var dot = 1.5; dot < width; dot += 3.5) {
                                context.beginPath();
                                context.arc(dot, 1.5, 1.1, 0, 2 * Math.PI);
                                context.fill();
                            }
                        }
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(eventPoint) {
                        editorMenu.openAt(editor.positionAt(eventPoint.position.x,
                                                            eventPoint.position.y));
                    }
                }

                // Control-click is the Mac's other right click.
                TapHandler {
                    acceptedModifiers: Qt.MetaModifier
                    enabled: win.macOS
                    onTapped: function(eventPoint) {
                        editorMenu.openAt(editor.positionAt(eventPoint.position.x,
                                                            eventPoint.position.y));
                    }
                }

                TapHandler {
                    acceptedModifiers: Qt.ControlModifier
                    onTapped: function(eventPoint) {
                        win.followLinkAt(editor.positionAt(eventPoint.position.x,
                                                           eventPoint.position.y));
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    text: "# Start writing"
                    visible: editor.text.length === 0 && !editor.activeFocus
                    color: win.mutedColor
                    font.family: editor.font.family
                    font.pixelSize: editor.font.pixelSize
                    font.weight: editor.font.weight
                }

                Component.onCompleted: {
                    backend.attachDocument(textDocument);
                    forceActiveFocus();
                }
            }
        }

        Row {
            id: footerStatus
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 12
            anchors.bottomMargin: 10
            spacing: 12
            opacity: 0.55

            FooterIconButton {
                objectName: "saveButton"
                iconName: "save"
                iconColor: win.mutedColor
                tooltip: "Save"
                onClicked: backend.save()
            }

            FooterIconButton {
                objectName: "openButton"
                iconName: "open"
                iconColor: win.mutedColor
                tooltip: "Open"
                onClicked: backend.openDialog()
            }

            FooterIconButton {
                id: fontButton
                objectName: "fontButton"
                iconName: "font"
                iconColor: win.mutedColor
                tooltip: "Font"
                onClicked: fontPicker.opened ? fontPicker.close() : fontPicker.open()
            }

            FooterIconButton {
                objectName: "previewButton"
                iconName: "preview"
                iconColor: win.mutedColor
                tooltip: "Preview in Browser"
                onClicked: backend.previewInBrowser()
            }

            FooterIconButton {
                id: themeButton
                objectName: "themeButton"
                iconName: "theme"
                iconColor: win.mutedColor
                tooltip: "Theme"
                onClicked: themePicker.opened ? themePicker.close() : themePicker.open()
            }

            Label {
                text: backend.status
                color: win.mutedColor
                font.family: "IBM Plex Mono"
                font.pixelSize: win.scaledSize(11)
                visible: text !== ""
                elide: Text.ElideRight
                width: Math.min(360, win.width / 3)
                height: win.scaledSize(16)
                verticalAlignment: Text.AlignVCenter
            }
        }

        FontPicker {
            id: fontPicker
            objectName: "fontPicker"
            x: footerStatus.x + fontButton.x - 8
            y: footerStatus.y - height - 10
            width: win.scaledSize(320)
            maximumHeight: Math.min(win.scaledSize(440), win.height - 80)
            fonts: backend.availableFonts
            currentFont: backend.editorFont
            defaultFont: "IBM Plex Mono"
            darkMode: win.darkMode
            textScale: win.textScale
            textColor: win.textColor
            mutedColor: win.mutedColor
            highlightColor: backend.themeAccent
            textZoom: backend.textZoom
            interfaceZoom: backend.interfaceZoom
            surfaceColor: win.surfaceColor
            wrapMode: backend.wrapMode
            onWrapRequested: backend.cycleWrapMode()
            onFontChosen: function(family) { backend.editorFont = family; }
            onTextZoomRequested: function(steps) { backend.zoomText(steps); }
            onInterfaceZoomRequested: function(steps) { backend.zoomInterface(steps); }
            onClosed: editor.forceActiveFocus()
        }

        ThemePicker {
            id: themePicker
            objectName: "themePicker"
            x: footerStatus.x + themeButton.x - 8
            y: footerStatus.y - height - 10
            themes: backend.themes
            currentTheme: backend.theme
            textScale: win.textScale
            textColor: win.textColor
            mutedColor: win.mutedColor
            highlightColor: backend.themeAccent
            surfaceColor: win.surfaceColor
            onPreviewRequested: function(theme) { backend.theme = theme; }
            onClosed: editor.forceActiveFocus()
        }

        Label {
            id: backlinkLabel
            objectName: "backlinkLabel"
            anchors.right: wordCountLabel.left
            anchors.bottom: parent.bottom
            anchors.rightMargin: 16
            anchors.bottomMargin: 10
            visible: win.backlinkCount > 0
            text: win.backlinkCount + (win.backlinkCount === 1 ? " Backlink" : " Backlinks")
            color: win.mutedColor
            opacity: backlinkHover.hovered ? 1 : 0.75
            font.family: "IBM Plex Mono"
            font.pixelSize: win.scaledSize(11)

            HoverHandler {
                id: backlinkHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: win.openNotePicker("backlinks")
            }
        }

        Label {
            id: wordCountLabel
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 12
            anchors.bottomMargin: 10
            text: backend.wordCount + (backend.wordCount === 1 ? " Word" : " Words")
            color: win.mutedColor
            opacity: 0.75
            font.family: "IBM Plex Mono"
            font.pixelSize: win.scaledSize(11)
        }


        Pane {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 12
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            height: win.scaledSize(win.replaceOpen ? 104 : 56)
            visible: win.searchOpen
            z: 10
            leftPadding: 16
            rightPadding: 8
            topPadding: 0
            bottomPadding: 0
            Material.elevation: 8

            background: Rectangle {
                radius: 9
                color: win.surfaceColor
            }

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextInput {
                        id: searchField
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: win.replaceOpen ? parent.height / 2 : parent.height
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        color: win.textColor
                        selectionColor: win.selectionFill
                        selectedTextColor: win.strongTextColor
                        font.pixelSize: win.scaledSize(17)
                        clip: true
                        onTextChanged: win.updateSearch()
                        Keys.onReturnPressed: function(event) {
                            win.moveSearch((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
                            event.accepted = true;
                        }
                        Keys.onEscapePressed: function(event) {
                            win.closeSearch();
                            event.accepted = true;
                        }
                    }

                    TextInput {
                        id: replaceField
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent.height / 2
                        visible: win.replaceOpen
                        verticalAlignment: TextInput.AlignVCenter
                        color: win.textColor
                        selectionColor: win.selectionFill
                        selectedTextColor: win.strongTextColor
                        font.pixelSize: win.scaledSize(17)
                        Keys.onReturnPressed: replaceCurrentButton.clicked()
                    }

                    Label {
                        anchors.verticalCenter: replaceField.verticalCenter
                        text: "Replace with"
                        visible: win.replaceOpen && replaceField.text.length === 0
                        color: win.mutedColor
                        font.pixelSize: win.scaledSize(17)
                    }

                    Label {
                        anchors.verticalCenter: searchField.verticalCenter
                        text: "Find"
                        visible: searchField.text.length === 0
                        color: win.mutedColor
                        font.pixelSize: win.scaledSize(17)
                    }
                }

                Label {
                    Layout.preferredWidth: win.scaledSize(58)
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: win.searchMatches.length === 0
                        ? "0/0"
                        : (win.searchMatchIndex + 1) + "/" + win.searchMatches.length
                    color: win.darkMode ? win.textColor : "#62635f"
                    font.pixelSize: win.scaledSize(16)
                }

                Button {
                    id: replaceCurrentButton
                    visible: win.replaceOpen
                    text: "Replace"
                    onClicked: {
                        if (win.searchMatchIndex < 0) return;
                        var start = win.searchMatches[win.searchMatchIndex];
                        EditorMutations.replaceRange(editor, start,
                                                     start + searchField.text.length,
                                                     replaceField.text);
                        win.updateSearch();
                    }
                }

                Button {
                    visible: win.replaceOpen
                    text: "All"
                    onClicked: {
                        if (searchField.text.length === 0) return;
                        for (var i = win.searchMatches.length - 1; i >= 0; --i) {
                            var start = win.searchMatches[i];
                            EditorMutations.replaceRange(editor, start,
                                                         start + searchField.text.length,
                                                         replaceField.text);
                        }
                        win.updateSearch();
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 34
                    color: win.darkMode ? "#6f6f62" : "#d5d56e"
                }

                SearchIconButton {
                    iconName: "up"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.moveSearch(-1)
                }

                SearchIconButton {
                    iconName: "down"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.moveSearch(1)
                }

                SearchIconButton {
                    iconName: "close"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.closeSearch()
                }
            }
        }
    }

    Component.onCompleted: {
        var geometry = backend.windowGeometry();
        if (geometry.x >= 0) x = geometry.x;
        if (geometry.y >= 0) y = geometry.y;
        width = geometry.width;
        height = geometry.height;
        if (geometry.maximized) showMaximized();
    }

    Component.onDestruction: backend.saveWindowGeometry(x, y, width, height, visibility === Window.Maximized)

}
