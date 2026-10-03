#pragma once

#include <QString>
#include <functional>
#include <QUrl>

#include "themes.h"

// A self-contained HTML page showing a note rendered in the browser, in the
// editor's theme and font. Markdown, math and code highlighting are done by
// marked, KaTeX and highlight.js from a CDN, so nothing is bundled.
namespace Preview {
struct Style {
    ThemePalette palette;
    bool dark = true;
    QString fontFamily;
    int fontPixels = 20;
};

// Turns an image reference into the URL the page loads it from.
using ImageResolver = std::function<QString(const QString &target)>;

QString html(const QString &markdown, const QString &title, const QUrl &baseUrl,
             const Style &style, const ImageResolver &resolveImage = {});
}
