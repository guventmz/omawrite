#pragma once

#include <QString>
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

QString html(const QString &markdown, const QString &title, const QUrl &baseUrl,
             const Style &style);

// The note as the page's render function takes it, for live updates.
QString noteJson(const QString &markdown);
}
