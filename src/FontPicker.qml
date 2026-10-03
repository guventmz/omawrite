import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Popup {
    id: picker

    property var fonts: []
    property string currentFont
    property string defaultFont
    property bool darkMode: true
    property real textScale: 1
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"
    property color highlightColor: "#428bca"
    property real maximumHeight: 440
    property real textZoom: 1
    property real interfaceZoom: 1
    property color surfaceColor: "#22221f"
    property string wrapMode: "column"
    property bool vimMode: false

    signal fontChosen(string family)
    signal textZoomRequested(int steps)
    signal interfaceZoomRequested(int steps)
    signal wrapRequested()
    signal vimRequested()

    readonly property var filteredFonts: {
        var query = filterField.text.trim().toLocaleLowerCase();
        if (query.length === 0)
            return fonts;
        return fonts.filter(function(family) {
            return family.toLocaleLowerCase().indexOf(query) !== -1;
        });
    }

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    function choose(family) {
        if (family)
            fontChosen(family);
        close();
    }

    // Shrink to fit when a filter leaves only a few fonts.
    readonly property real headerHeight: scaledSize(34) + 13
    readonly property real sizeControlsHeight: scaledSize(128) + 13
    height: Math.min(maximumHeight, topPadding + bottomPadding + headerHeight
                     + Math.max(scaledSize(36), fontList.contentHeight) + sizeControlsHeight)

    component StepButton: Label {
        signal activated()
        width: picker.scaledSize(28)
        height: parent.height
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: stepHover.hovered ? picker.textColor : picker.mutedColor
        font.family: picker.defaultFont
        font.pixelSize: picker.scaledSize(16)

        HoverHandler {
            id: stepHover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: parent.activated()
        }
    }

    component SizeStepper: Item {
        id: stepper

        property string label
        property real zoom: 1
        signal stepped(int steps)

        width: parent.width
        height: picker.scaledSize(32)

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: stepper.label
            color: picker.textColor
            font.family: picker.defaultFont
            font.pixelSize: picker.scaledSize(13)
        }

        Row {
            anchors.right: parent.right
            height: parent.height

            StepButton {
                text: "−"
                onActivated: stepper.stepped(-1)
            }

            Label {
                width: picker.scaledSize(48)
                height: parent.height
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: Math.round(stepper.zoom * 100) + "%"
                color: zoomHover.hovered ? picker.textColor : picker.mutedColor
                font.family: picker.defaultFont
                font.pixelSize: picker.scaledSize(12)
                ToolTip.visible: zoomHover.hovered
                ToolTip.text: "Reset"

                HoverHandler {
                    id: zoomHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: stepper.stepped(0)
                }
            }

            StepButton {
                text: "+"
                onActivated: stepper.stepped(1)
            }
        }
    }

    modal: false
    focus: true
    padding: 8
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.elevation: 8

    onAboutToShow: {
        filterField.text = "";
        fontList.currentIndex = Math.max(0, fonts.indexOf(currentFont));
        fontList.positionViewAtIndex(fontList.currentIndex, ListView.Center);
        filterField.forceActiveFocus();
    }

    background: Rectangle {
        radius: 9
        color: picker.surfaceColor
    }

    contentItem: Column {
        spacing: 6

        Item {
            width: parent.width
            height: picker.scaledSize(34)

            TextInput {
                id: filterField
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                color: picker.textColor
                selectByMouse: true
                clip: true
                font.family: picker.defaultFont
                font.pixelSize: picker.scaledSize(14)
                onTextChanged: fontList.currentIndex = 0

                Keys.onUpPressed: fontList.decrementCurrentIndex()
                Keys.onDownPressed: fontList.incrementCurrentIndex()
                Keys.onReturnPressed: picker.choose(picker.filteredFonts[fontList.currentIndex])
                Keys.onEnterPressed: picker.choose(picker.filteredFonts[fontList.currentIndex])
            }

            Label {
                anchors.left: filterField.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Filter fonts"
                visible: filterField.text.length === 0
                color: picker.mutedColor
                font: filterField.font
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: picker.mutedColor
            opacity: 0.3
        }

        ListView {
            id: fontList
            objectName: "fontList"
            width: parent.width
            height: picker.availableHeight - picker.headerHeight - picker.sizeControlsHeight
            clip: true
            model: picker.filteredFonts
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: ItemDelegate {
                id: row
                required property string modelData
                required property int index

                width: fontList.width
                height: picker.scaledSize(36)
                leftPadding: 8
                rightPadding: 8
                highlighted: ListView.isCurrentItem
                onClicked: picker.choose(modelData)

                background: Rectangle {
                    radius: 5
                    color: picker.highlightColor
                    opacity: row.highlighted ? 0.22 : (row.hovered ? 0.12 : 0)
                }

                contentItem: Item {
                    Label {
                        anchors.left: parent.left
                        anchors.right: badge.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData
                        elide: Text.ElideRight
                        color: picker.textColor
                        font.family: row.modelData
                        font.pixelSize: picker.scaledSize(16)
                    }

                    Label {
                        id: badge
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: [row.modelData === picker.currentFont ? "✓" : "",
                               row.modelData === picker.defaultFont ? "Default" : ""]
                              .filter(function(part) { return part.length > 0; }).join(" ")
                        color: picker.mutedColor
                        font.family: picker.defaultFont
                        font.pixelSize: picker.scaledSize(11)
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: picker.mutedColor
            opacity: 0.3
        }

        SizeStepper {
            objectName: "textSizeStepper"
            label: "Text size"
            zoom: picker.textZoom
            onStepped: function(steps) { picker.textZoomRequested(steps); }
        }

        Item {
            objectName: "wrapToggle"
            width: parent.width
            height: picker.scaledSize(32)

            Label {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Wrap"
                color: picker.textColor
                font.family: picker.defaultFont
                font.pixelSize: picker.scaledSize(13)
            }

            Label {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: ({ column: "Column", window: "Window width", off: "Off" })[picker.wrapMode] + "  ›"
                color: wrapHover.hovered ? picker.textColor : picker.mutedColor
                font.family: picker.defaultFont
                font.pixelSize: picker.scaledSize(12)
            }

            HoverHandler {
                id: wrapHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: picker.wrapRequested()
            }
        }

        Item {
            objectName: "vimToggle"
            width: parent.width
            height: picker.scaledSize(32)

            Label {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Vim mode"
                color: picker.textColor
                font.family: picker.defaultFont
                font.pixelSize: picker.scaledSize(13)
            }

            Label {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: (picker.vimMode ? "On" : "Off") + "  ›"
                color: vimHover.hovered ? picker.textColor : picker.mutedColor
                font.family: picker.defaultFont
                font.pixelSize: picker.scaledSize(12)
            }

            HoverHandler {
                id: vimHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: picker.vimRequested()
            }
        }

        SizeStepper {
            objectName: "interfaceSizeStepper"
            label: "Interface size"
            zoom: picker.interfaceZoom
            onStepped: function(steps) { picker.interfaceZoomRequested(steps); }
        }
    }
}
