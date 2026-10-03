#include "themes.h"

namespace {
struct ThemeDefinition {
    ThemeInfo info;
    ThemePalette light;
    ThemePalette dark;
};

//  background, foreground, heading, accent, selection, marker,
//  code background, math, keyword, string, comment, number
const QList<ThemeDefinition> &definitions() {
    static const QList<ThemeDefinition> themes = {
        {{QStringLiteral("omawrite"), QStringLiteral("Omawrite")},
         {"#ffffff", "#222324", "#222324", "#2077b2", "#2077b2", "#aeb1b5",
          "#f4f4f4", "#7a3fa8", "#a626a4", "#3f8a3f", "#a0a1a7", "#986801"},
         {"#101010", "#eeeeee", "#ffffff", "#5584aa", "#186a9a", "#4f525a",
          "#1c1a1a", "#c792ea", "#c678dd", "#98c379", "#7f848e", "#d19a66"}},
        {{QStringLiteral("paper"), QStringLiteral("Paper")},
         {"#f6f1e7", "#2f2a24", "#9c3d10", "#1f6f8b", "#d9c7a3", "#b3a58f",
          "#ece4d4", "#7a4f9a", "#8b3a8b", "#4f7a28", "#9a8f7d", "#a0522d"},
         {"#1c1a17", "#e8e0d0", "#e6a65d", "#7fb3c8", "#4a4030", "#6b6255",
          "#26231f", "#c9a0dc", "#d38bc8", "#a9c97a", "#857a6a", "#e0a070"}},
        {{QStringLiteral("nord"), QStringLiteral("Nord")},
         {"#eceff4", "#2e3440", "#5e81ac", "#5e81ac", "#d8dee9", "#9aa5b8",
          "#e5e9f0", "#9a6a8f", "#5e81ac", "#5f7f45", "#8a94a8", "#a0688f"},
         {"#2e3440", "#d8dee9", "#88c0d0", "#81a1c1", "#434c5e", "#616e88",
          "#3b4252", "#b48ead", "#81a1c1", "#a3be8c", "#7b88a1", "#b48ead"}},
        {{QStringLiteral("solarized"), QStringLiteral("Solarized")},
         {"#fdf6e3", "#586e75", "#cb4b16", "#268bd2", "#eee8d5", "#93a1a1",
          "#eee8d5", "#6c71c4", "#859900", "#2aa198", "#93a1a1", "#d33682"},
         {"#002b36", "#93a1a1", "#b58900", "#268bd2", "#073642", "#586e75",
          "#073642", "#8b8fd6", "#859900", "#2aa198", "#657b83", "#d33682"}},
        {{QStringLiteral("gruvbox"), QStringLiteral("Gruvbox")},
         {"#fbf1c7", "#3c3836", "#af3a03", "#076678", "#ebdbb2", "#a89984",
          "#f2e5bc", "#8f3f71", "#9d0006", "#79740e", "#928374", "#8f3f71"},
         {"#282828", "#ebdbb2", "#fabd2f", "#83a598", "#504945", "#7c6f64",
          "#32302f", "#d3869b", "#fb4934", "#b8bb26", "#928374", "#d3869b"}},
        {{QStringLiteral("contrast"), QStringLiteral("High Contrast")},
         {"#ffffff", "#000000", "#0033aa", "#0055cc", "#b3d4ff", "#666666",
          "#f0f0f0", "#6a00a8", "#a0006a", "#2a6a00", "#5c5c5c", "#9a4a00"},
         {"#000000", "#ffffff", "#ffd75f", "#5fd7ff", "#264f78", "#8a8a8a",
          "#1a1a1a", "#d7afff", "#ff87d7", "#afff87", "#9e9e9e", "#ffaf5f"}},
    };
    return themes;
}

const ThemeDefinition *find(const QString &id) {
    for (const ThemeDefinition &theme : definitions()) {
        if (theme.info.id == id)
            return &theme;
    }
    return nullptr;
}
}

QString Themes::defaultId() {
    return QStringLiteral("omawrite");
}

QList<ThemeInfo> Themes::all() {
    QList<ThemeInfo> themes;
    for (const ThemeDefinition &theme : definitions())
        themes.append(theme.info);
    return themes;
}

bool Themes::exists(const QString &id) {
    return find(id) != nullptr;
}

ThemePalette Themes::palette(const QString &id, bool dark) {
    const ThemeDefinition *theme = find(id);
    if (!theme)
        theme = find(defaultId());
    return dark ? theme->dark : theme->light;
}
