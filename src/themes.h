#pragma once

#include <QList>
#include <QString>

// Colors for the writing surface and the Markdown highlighter. Every theme
// has a light and a dark variant; the desktop's color scheme picks one.
struct ThemePalette {
    QString background;
    QString foreground;
    QString heading;
    QString accent;
    QString selection;
    QString marker;
    QString codeBackground;
    QString math;
    QString keyword;
    QString string;
    QString comment;
    QString number;
};

struct ThemeInfo {
    QString id;
    QString name;
};

namespace Themes {
QString defaultId();
QList<ThemeInfo> all();
bool exists(const QString &id);
ThemePalette palette(const QString &id, bool dark);
}
