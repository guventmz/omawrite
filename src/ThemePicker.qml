import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Arrow keys preview a theme live; Enter or a click keeps it, Escape or a
// click outside goes back to the one you started with.
Popup {
    id: picker

    property var themes: []
    property string currentTheme
    property string fontFamily: "IBM Plex Mono"
    property real textScale: 1
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"
    property color highlightColor: "#428bca"
    property color surfaceColor: "#22221f"

    signal previewRequested(string theme)

    property string originalTheme
    property bool committed: false

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    function indexOfTheme(id) {
        for (var i = 0; i < themes.length; ++i) {
            if (themes[i].id === id)
                return i;
        }
        return 0;
    }

    function preview(index) {
        themeList.currentIndex = index;
        previewRequested(themes[index].id);
    }

    function commit(index) {
        preview(index);
        committed = true;
        close();
    }

    width: scaledSize(300)
    height: topPadding + bottomPadding + themeList.contentHeight
    modal: false
    focus: true
    padding: 8
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.elevation: 8

    onAboutToShow: {
        originalTheme = currentTheme;
        committed = false;
        themeList.currentIndex = indexOfTheme(currentTheme);
        themeList.forceActiveFocus();
    }

    onClosed: {
        if (!committed)
            previewRequested(originalTheme);
    }

    background: Rectangle {
        radius: 9
        color: picker.surfaceColor
    }

    contentItem: ListView {
        id: themeList
        objectName: "themeList"
        model: picker.themes
        interactive: false
        focus: true
        highlightMoveDuration: 0

        Keys.onPressed: function(event) {
            var control = Qt.platform.os === "osx" ? (event.modifiers & Qt.MetaModifier)
                                                   : (event.modifiers & Qt.ControlModifier);
            if (control && event.key === Qt.Key_N) {
                picker.preview(Math.min(count - 1, currentIndex + 1));
                event.accepted = true;
            } else if (control && event.key === Qt.Key_P) {
                picker.preview(Math.max(0, currentIndex - 1));
                event.accepted = true;
            }
        }
        Keys.onUpPressed: picker.preview(Math.max(0, currentIndex - 1))
        Keys.onDownPressed: picker.preview(Math.min(count - 1, currentIndex + 1))
        Keys.onReturnPressed: picker.commit(currentIndex)
        Keys.onEnterPressed: picker.commit(currentIndex)

        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index

            width: themeList.width
            height: picker.scaledSize(40)
            leftPadding: 8
            rightPadding: 8
            highlighted: ListView.isCurrentItem
            onClicked: picker.commit(index)

            background: Rectangle {
                radius: 5
                color: picker.highlightColor
                opacity: row.highlighted ? 0.22 : (row.hovered ? 0.12 : 0)
            }

            contentItem: Item {
                Rectangle {
                    id: swatch
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: picker.scaledSize(52)
                    height: picker.scaledSize(26)
                    radius: 4
                    color: row.modelData.background
                    border.width: 1
                    border.color: Qt.rgba(0.5, 0.5, 0.5, 0.35)

                    Row {
                        anchors.centerIn: parent
                        spacing: picker.scaledSize(4)

                        Label {
                            text: "Aa"
                            color: row.modelData.heading
                            font.family: picker.fontFamily
                            font.bold: true
                            font.pixelSize: picker.scaledSize(13)
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: picker.scaledSize(6)
                            height: width
                            radius: width / 2
                            color: row.modelData.accent
                        }
                    }
                }

                Label {
                    anchors.left: swatch.right
                    anchors.leftMargin: 12
                    anchors.right: check.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.name
                    elide: Text.ElideRight
                    color: picker.textColor
                    font.family: picker.fontFamily
                    font.pixelSize: picker.scaledSize(15)
                }

                Label {
                    id: check
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.id === picker.originalTheme ? "✓" : ""
                    color: picker.mutedColor
                    font.pixelSize: picker.scaledSize(13)
                }
            }
        }
    }
}
