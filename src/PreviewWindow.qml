import QtQuick
import QtQuick.Window
import QtWebView

// The rendered note in its own window beside the editor, drawn by the
// platform's web view. The editor pushes fresh text into the page as you
// type, so the scroll position holds.
Window {
    id: previewWindow

    property string noteTitle

    title: noteTitle + " — Preview"
    flags: Qt.Window
    color: backend.themeBackground

    // The page arrives as HTML: the macOS web view will not open file URLs.
    function load(html) {
        webView.loadHtml(html, "about:blank");
    }

    function render(noteJson) {
        webView.runJavaScript("window.omawriteRender && window.omawriteRender(" + noteJson + ")");
    }

    WebView {
        id: webView
        objectName: "previewWebView"
        anchors.fill: parent
    }

    Shortcut {
        sequences: ["Ctrl+W", "Escape"]
        onActivated: previewWindow.close()
    }
}
