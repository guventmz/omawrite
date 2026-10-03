#include "preview.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

#include "markdownhighlighter.h"

namespace {
const QString markedUrl =
    QStringLiteral("https://cdnjs.cloudflare.com/ajax/libs/marked/18.0.14/lib/marked.umd.min.js");
const QString katexScriptUrl =
    QStringLiteral("https://cdnjs.cloudflare.com/ajax/libs/KaTeX/0.18.9/katex.min.js");
const QString katexStyleUrl =
    QStringLiteral("https://cdnjs.cloudflare.com/ajax/libs/KaTeX/0.18.9/katex.min.css");
const QString highlightUrl =
    QStringLiteral("https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.11.2/highlight.min.js");

QString mathToken(int index) {
    return QStringLiteral("OMAWRITEMATH%1X").arg(index);
}

// marked would read the _ and \ inside math as Markdown, so every formula is
// swapped for a token first and rendered with KaTeX afterwards. Code blocks
// and inline code are left alone, using the same rules as the editor.
QString protectMath(const QString &markdown, QJsonArray *math,
                    const Preview::ImageResolver &resolveImage) {
    static const QRegularExpression fenceRe(QStringLiteral("^\\s{0,3}(```|~~~)"));
    static const QRegularExpression wikiLinkRe(
        QStringLiteral("\\[\\[([^\\[\\]\\n|]+?)(?:\\|([^\\[\\]\\n]+?))?\\]\\]"));
    const auto addMath = [math](const QString &latex, bool display) {
        math->append(QJsonObject{{QStringLiteral("latex"), latex.trimmed()},
                                 {QStringLiteral("display"), display}});
        return mathToken(int(math->size()) - 1);
    };

    QStringList out;
    bool inCode = false;
    bool inMath = false;
    QStringList mathLines;
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        if (!inMath && fenceRe.match(line).hasMatch()) {
            inCode = !inCode;
            out.append(line);
            continue;
        }
        if (inCode) {
            out.append(line);
            continue;
        }
        if (inMath) {
            const int close = int(line.indexOf(QStringLiteral("$$")));
            if (close < 0) {
                mathLines.append(line);
                continue;
            }
            mathLines.append(line.left(close));
            out << QString() << addMath(mathLines.join(QLatin1Char('\n')), true) << QString();
            inMath = false;
            continue;
        }
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("$$")) && trimmed.count(QStringLiteral("$$")) == 1) {
            inMath = true;
            mathLines = {trimmed.mid(2)};
            continue;
        }

        struct Replacement {
            int start;
            int length;
            QString text;
        };
        QList<Replacement> replacements;
        const QList<MarkdownHighlighter::VerbatimSpan> verbatim = MarkdownHighlighter::verbatimSpans(line);
        for (const MarkdownHighlighter::VerbatimSpan &span : verbatim) {
            const QString source = line.mid(span.span.start, span.span.length);
            if (span.image) {
                // ![[name|300]] and ![alt](name) both become a plain image
                // pointing at the file the vault resolves.
                const bool embed = source.startsWith(QStringLiteral("![["));
                const QString target = embed ? source.mid(3, source.length() - 5)
                                             : source.section(QStringLiteral("]("), 1).chopped(1);
                const QString alt = embed ? target.section(QLatin1Char('|'), 0, 0).trimmed()
                                          : source.mid(2, source.indexOf(QStringLiteral("](")) - 2);
                const QString url = resolveImage ? resolveImage(target) : QString();
                if (!url.isEmpty())
                    replacements.append({span.span.start, span.span.length,
                                         QStringLiteral("![%1](<%2>)").arg(alt, url)});
                else if (embed)
                    replacements.append({span.span.start, span.span.length,
                                         QStringLiteral("<span class=\"missing\">%1</span>")
                                             .arg(alt.toHtmlEscaped())});
                continue;
            }
            if (!span.math)
                continue;
            const bool display = source.startsWith(QStringLiteral("$$"));
            const int marker = display ? 2 : 1;
            replacements.append({span.span.start, span.span.length,
                                 addMath(source.mid(marker, source.length() - 2 * marker), display)});
        }
        QRegularExpressionMatchIterator links = wikiLinkRe.globalMatch(line);
        while (links.hasNext()) {
            const QRegularExpressionMatch link = links.next();
            const int start = int(link.capturedStart());
            const bool inVerbatim = std::any_of(verbatim.begin(), verbatim.end(), [start](const auto &span) {
                return start >= span.span.start && start < span.span.start + span.span.length;
            });
            if (inVerbatim)
                continue;
            const QString shown = link.capturedStart(2) >= 0 ? link.captured(2) : link.captured(1);
            replacements.append({start, int(link.capturedLength()),
                                 QStringLiteral("<span class=\"wikilink\">%1</span>")
                                     .arg(shown.trimmed().toHtmlEscaped())});
        }

        QString result = line;
        std::sort(replacements.begin(), replacements.end(), [](const auto &a, const auto &b) {
            return a.start > b.start;
        });
        for (const Replacement &replacement : std::as_const(replacements))
            result.replace(replacement.start, replacement.length, replacement.text);
        out.append(result);
    }
    if (inMath)
        out.append(QStringLiteral("$$") + mathLines.join(QLatin1Char('\n')));
    return out.join(QLatin1Char('\n'));
}

// Chrome refuses web fonts from file:// pages, so the bundled typeface rides
// along inside the page itself.
QString embeddedFontFaces() {
    struct Face {
        const char *file;
        const char *weight;
        const char *style;
    };
    static const Face faces[] = {{"IBMPlexMono-Regular", "400", "normal"},
                                 {"IBMPlexMono-Italic", "400", "italic"},
                                 {"IBMPlexMono-Bold", "700", "normal"},
                                 {"IBMPlexMono-BoldItalic", "700", "italic"}};
    QString css;
    for (const Face &face : faces) {
        QFile file(QStringLiteral(":/fonts/%1.ttf").arg(QLatin1String(face.file)));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        css += QStringLiteral("@font-face { font-family: 'IBM Plex Mono'; font-weight: %1; "
                              "font-style: %2; src: url(data:font/ttf;base64,%3) format('truetype'); }\n")
                   .arg(QLatin1String(face.weight), QLatin1String(face.style),
                        QString::fromLatin1(file.readAll().toBase64()));
    }
    return css;
}

QString styleSheet(const Preview::Style &style) {
    const ThemePalette &p = style.palette;
    return QStringLiteral(R"(
:root { color-scheme: %1; }
html, body { margin: 0; background: %2; }
body { color: %3; font-family: '%4', 'IBM Plex Mono', ui-monospace, monospace; font-size: %5px;
       line-height: 1.4; -webkit-font-smoothing: antialiased; }
main { max-width: 65ch; margin: 0 auto; padding: 48px 24px 160px; }
h1, h2, h3, h4, h5, h6 { color: %6; font-size: 1em; font-weight: 700; margin: 1.4em 0 0.7em; }
a, .wikilink { color: %7; text-decoration: underline; }
code, pre { font-family: 'IBM Plex Mono', ui-monospace, monospace; background: %8; border-radius: 4px; }
code { padding: 0.1em 0.3em; }
pre { padding: 0.8em 1em; overflow-x: auto; }
pre code { padding: 0; background: none; }
blockquote { margin: 1em 0; padding-left: 1em; border-left: 3px solid %9; color: %9; font-style: italic; }
hr { border: none; border-top: 1px solid %9; margin: 2em 0; }
table { border-collapse: collapse; }
th, td { border: 1px solid %9; padding: 0.3em 0.6em; }
img { max-width: 100%; border-radius: 4px; }
.missing { color: %9; font-style: italic; }
.missing::before { content: "missing image: "; }
::selection { background: %10; }
.katex { font-size: 1.1em; }
.katex-display { overflow-x: auto; overflow-y: hidden; }
.hljs-keyword, .hljs-built_in, .hljs-literal, .hljs-type, .hljs-selector-tag { color: %11; }
.hljs-string, .hljs-regexp, .hljs-template-string { color: %12; }
.hljs-comment, .hljs-quote { color: %13; font-style: italic; }
.hljs-number { color: %14; }
)")
        .arg(style.dark ? QStringLiteral("dark") : QStringLiteral("light"), p.background,
             p.foreground, style.fontFamily, QString::number(style.fontPixels), p.heading,
             p.accent, p.codeBackground, p.marker)
        .arg(p.selection, p.keyword, p.string, p.comment, p.number);
}

const char *renderScript = R"(
(function () {
  var note = JSON.parse(document.getElementById('note').textContent);
  var content = document.getElementById('content');
  if (typeof marked === 'undefined') {
    var pre = document.createElement('pre');
    pre.textContent = note.source;
    content.appendChild(pre);
    return;
  }
  var html = marked.parse(note.markdown, { gfm: true });
  html = html.replace(/OMAWRITEMATH(\d+)X/g, function (_, index) {
    var math = note.math[+index];
    if (typeof katex === 'undefined')
      return '<code>' + math.latex.replace(/[&<>]/g, function (c) {
        return { '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c];
      }) + '</code>';
    return katex.renderToString(math.latex, { displayMode: math.display, throwOnError: false });
  });
  content.innerHTML = html;
  if (typeof hljs !== 'undefined')
    content.querySelectorAll('pre code').forEach(function (block) { hljs.highlightElement(block); });
})();
)";
}

QString Preview::html(const QString &markdown, const QString &title, const QUrl &baseUrl,
                      const Style &style, const ImageResolver &resolveImage) {
    QJsonArray math;
    const QString protectedMarkdown = protectMath(markdown, &math, resolveImage);
    const QJsonObject note{{QStringLiteral("markdown"), protectedMarkdown},
                           {QStringLiteral("source"), markdown},
                           {QStringLiteral("math"), math}};
    // A note mentioning "</script>" must not end the data block early.
    const QString noteJson = QString::fromUtf8(QJsonDocument(note).toJson(QJsonDocument::Compact))
                                 .replace(QStringLiteral("</"), QStringLiteral("<\\/"));

    QString page;
    page += QStringLiteral("<!doctype html>\n<html><head><meta charset=\"utf-8\">\n");
    page += QStringLiteral("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n");
    page += QStringLiteral("<title>%1</title>\n").arg(title.toHtmlEscaped());
    if (baseUrl.isValid())
        page += QStringLiteral("<base href=\"%1\">\n").arg(baseUrl.toString().toHtmlEscaped());
    page += QStringLiteral("<link rel=\"stylesheet\" href=\"%1\">\n").arg(katexStyleUrl);
    page += QStringLiteral("<style>\n") + embeddedFontFaces() + styleSheet(style)
        + QStringLiteral("</style>\n</head><body>\n<main id=\"content\"></main>\n");
    page += QStringLiteral("<script id=\"note\" type=\"application/json\">") + noteJson
        + QStringLiteral("</script>\n");
    for (const QString &script : {markedUrl, katexScriptUrl, highlightUrl})
        page += QStringLiteral("<script src=\"%1\"></script>\n").arg(script);
    page += QStringLiteral("<script>") + QString::fromLatin1(renderScript)
        + QStringLiteral("</script>\n</body></html>\n");
    return page;
}
