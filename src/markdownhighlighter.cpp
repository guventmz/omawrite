#include "markdownhighlighter.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QTextDocument>

#include <algorithm>

namespace {
const QString codeFontFamily = QStringLiteral("IBM Plex Mono");
constexpr qreal headingScales[6] = {1.5, 1.3, 1.15, 1.05, 1.0, 1.0};

const QRegularExpression &fenceRe() {
    static const QRegularExpression re(QStringLiteral("^\\s{0,3}(```|~~~)\\s*([\\w+#.-]*)"));
    return re;
}

bool startsLineComment(const QString &text, int i, MarkdownHighlighter::CommentStyle style) {
    using Style = MarkdownHighlighter::CommentStyle;
    const QChar c = text.at(i);
    const QChar next = i + 1 < text.length() ? text.at(i + 1) : QChar();
    const bool hash = c == QLatin1Char('#');
    const bool slash = c == QLatin1Char('/') && next == QLatin1Char('/');
    const bool dash = c == QLatin1Char('-') && next == QLatin1Char('-');
    switch (style) {
    case Style::Any: return hash || slash;
    case Style::Hash: return hash;
    case Style::Slash: return slash;
    case Style::Dash: return dash;
    case Style::None: return false;
    }
    return false;
}
}

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document), m_palette(Themes::palette(Themes::defaultId(), true)) {
    rebuildFormats();
}

void MarkdownHighlighter::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    rebuildFormats();
    rehighlight();
}

// The hidden marker spacing and heading sizes are measured from the document's
// default font, so they have to be remeasured whenever the editor font changes.
void MarkdownHighlighter::refreshFont() {
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setPalette(const ThemePalette &palette) {
    m_palette = palette;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart)
        return;
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    rehighlight();
}

bool MarkdownHighlighter::isVerbatimState(int state) {
    return state == MathState || state >= CodeState;
}

MarkdownHighlighter::CommentStyle MarkdownHighlighter::commentStyleFor(const QString &language) {
    static const QStringList hash = {
        QStringLiteral("python"), QStringLiteral("py"), QStringLiteral("bash"),
        QStringLiteral("sh"), QStringLiteral("zsh"), QStringLiteral("shell"),
        QStringLiteral("console"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("toml"), QStringLiteral("ruby"), QStringLiteral("rb"),
        QStringLiteral("r"), QStringLiteral("perl"), QStringLiteral("make"),
        QStringLiteral("makefile"), QStringLiteral("dockerfile"), QStringLiteral("elixir"),
        QStringLiteral("julia"), QStringLiteral("nim"), QStringLiteral("powershell"),
        QStringLiteral("ini"), QStringLiteral("conf")};
    static const QStringList slash = {
        QStringLiteral("c"), QStringLiteral("cpp"), QStringLiteral("c++"),
        QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("cs"),
        QStringLiteral("csharp"), QStringLiteral("java"), QStringLiteral("js"),
        QStringLiteral("javascript"), QStringLiteral("jsx"), QStringLiteral("ts"),
        QStringLiteral("typescript"), QStringLiteral("tsx"), QStringLiteral("go"),
        QStringLiteral("rust"), QStringLiteral("rs"), QStringLiteral("swift"),
        QStringLiteral("kotlin"), QStringLiteral("kt"), QStringLiteral("scala"),
        QStringLiteral("dart"), QStringLiteral("php"), QStringLiteral("css"),
        QStringLiteral("scss"), QStringLiteral("zig"), QStringLiteral("qml"),
        QStringLiteral("glsl"), QStringLiteral("objc")};
    static const QStringList dash = {
        QStringLiteral("sql"), QStringLiteral("lua"), QStringLiteral("haskell"),
        QStringLiteral("hs"), QStringLiteral("elm")};
    static const QStringList none = {
        QStringLiteral("json"), QStringLiteral("text"), QStringLiteral("txt"),
        QStringLiteral("plain"), QStringLiteral("markdown"), QStringLiteral("md"),
        QStringLiteral("latex"), QStringLiteral("tex"), QStringLiteral("math"),
        QStringLiteral("diff"), QStringLiteral("csv")};

    const QString lang = language.toLower();
    if (lang.isEmpty())
        return CommentStyle::Any;
    if (hash.contains(lang))
        return CommentStyle::Hash;
    if (slash.contains(lang))
        return CommentStyle::Slash;
    if (dash.contains(lang))
        return CommentStyle::Dash;
    if (none.contains(lang))
        return CommentStyle::None;
    return CommentStyle::Any;
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor background(m_palette.background);
    const QColor text(m_palette.foreground);
    const QColor marker(m_palette.marker);

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(background);
    m_hiddenMarkerFormat.setFontPointSize(1.0);

    const QFont baseFont = document() ? document()->defaultFont() : QFont();
    QFont hiddenFont = baseFont;
    hiddenFont.setPointSizeF(1.0);
    const qreal charWidth = QFontMetricsF(hiddenFont).horizontalAdvance(QLatin1Char('['));

    m_hiddenMarkerFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_hiddenMarkerFormat.setFontLetterSpacing(-charWidth);

    const int basePixels = baseFont.pixelSize() > 0 ? baseFont.pixelSize() : 20;
    for (int level = 0; level < 6; ++level) {
        const int pixels = qRound(basePixels * headingScales[level]);
        m_headingFormats[level] = QTextCharFormat();
        m_headingFormats[level].setForeground(QColor(m_palette.heading));
        m_headingFormats[level].setFontWeight(QFont::Bold);
        m_headingFormats[level].setProperty(QTextFormat::FontPixelSize, pixels);
        m_headingMarkerFormats[level] = m_markerFormat;
        m_headingMarkerFormats[level].setProperty(QTextFormat::FontPixelSize, pixels);
    }

    m_boldFormat = QTextCharFormat();
    m_boldFormat.setFontWeight(QFont::Bold);
    m_boldFormat.setForeground(text);

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);
    m_italicFormat.setForeground(text);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    m_codeFormat.setBackground(QColor(m_palette.codeBackground));
    m_codeFormat.setFontFamilies({codeFontFamily});
    m_codeFormat.setFontFixedPitch(true);

    m_fenceFormat = m_codeFormat;
    m_fenceFormat.setForeground(marker);

    const auto codeColored = [this](const QString &color) {
        QTextCharFormat format = m_codeFormat;
        format.setForeground(QColor(color));
        return format;
    };
    m_keywordFormat = codeColored(m_palette.keyword);
    m_stringFormat = codeColored(m_palette.string);
    m_commentFormat = codeColored(m_palette.comment);
    m_commentFormat.setFontItalic(true);
    m_numberFormat = codeColored(m_palette.number);

    m_mathFormat = QTextCharFormat();
    m_mathFormat.setForeground(QColor(m_palette.math));
    m_mathFormat.setFontFamilies({codeFontFamily});

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(marker);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(QColor(m_palette.accent));
    m_linkFormat.setFontUnderline(true);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    setCurrentBlockState(NormalState);
    if (!highlightFencedCode(text) && !highlightMathBlock(text) && !text.isEmpty()) {
        highlightMarkers(text);
        if (text.contains(QLatin1Char('`')) || text.contains(QLatin1Char('*'))
            || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('['))
            || text.contains(QLatin1Char('$'))) {
            highlightInline(text);
        }
    }
    highlightSearch(text);
}

bool MarkdownHighlighter::highlightFencedCode(const QString &text) {
    const int previous = previousBlockState();
    const QRegularExpressionMatch fence = fenceRe().match(text);
    if (previous >= CodeState) {
        if (fence.hasMatch()) {
            setFormat(0, int(text.length()), m_fenceFormat);
            return true;
        }
        setCurrentBlockState(previous);
        highlightCodeLine(text, CommentStyle(previous - CodeState));
        return true;
    }

    if (!fence.hasMatch())
        return false;
    setFormat(0, int(text.length()), m_fenceFormat);
    setCurrentBlockState(CodeState + int(commentStyleFor(fence.captured(2))));
    return true;
}

bool MarkdownHighlighter::highlightMathBlock(const QString &text) {
    const bool insideMath = previousBlockState() == MathState;
    const QString trimmed = text.trimmed();
    const bool opens = !insideMath && trimmed.startsWith(QStringLiteral("$$"))
        && trimmed.count(QStringLiteral("$$")) == 1;
    if (!insideMath && !opens)
        return false;

    setFormat(0, int(text.length()), m_mathFormat);
    for (int at = int(text.indexOf(QStringLiteral("$$"))); at >= 0;
         at = int(text.indexOf(QStringLiteral("$$"), at + 2)))
        setFormat(at, 2, m_markerFormat);

    // A math block runs until a line holding the closing $$.
    if (opens || !text.contains(QStringLiteral("$$")))
        setCurrentBlockState(MathState);
    return true;
}

// Deliberately small: keywords, numbers, strings and comments are what make
// code readable at a glance; anything fancier belongs in a real IDE.
void MarkdownHighlighter::highlightCodeLine(const QString &text, CommentStyle style) {
    const int length = int(text.length());
    setFormat(0, length, m_codeFormat);
    if (length == 0)
        return;

    static const QRegularExpression keywordRe(QStringLiteral(
        "\\b(?:if|else|elif|for|while|do|return|function|func|fn|def|class|struct|enum|"
        "interface|import|from|export|package|module|use|let|const|var|val|mut|pub|static|"
        "public|private|protected|new|delete|this|self|super|true|false|null|nil|None|True|"
        "False|undefined|try|catch|except|finally|raise|throw|async|await|yield|in|is|not|"
        "and|or|as|with|match|case|switch|break|continue|default|lambda|void|int|float|"
        "double|bool|char|string|auto|type|impl|trait|where|then|end|"
        "SELECT|FROM|WHERE|INSERT|INTO|VALUES|UPDATE|SET|DELETE|CREATE|TABLE|JOIN|ON|"
        "GROUP|ORDER|BY|LIMIT|AND|OR|NOT|NULL|AS)\\b"));
    static const QRegularExpression numberRe(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"));
    for (const QRegularExpression *re : {&keywordRe, &numberRe}) {
        QRegularExpressionMatchIterator matches = re->globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            setFormat(int(match.capturedStart()), int(match.capturedLength()),
                      re == &keywordRe ? m_keywordFormat : m_numberFormat);
        }
    }

    // Strings and comments last, since they override everything inside them.
    int stringStart = -1;
    QChar quote;
    for (int i = 0; i < length; ++i) {
        const QChar c = text.at(i);
        if (stringStart >= 0) {
            if (c == QLatin1Char('\\')) {
                ++i;
            } else if (c == quote) {
                setFormat(stringStart, i - stringStart + 1, m_stringFormat);
                stringStart = -1;
            }
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            quote = c;
            stringStart = i;
        } else if ((style == CommentStyle::Slash || style == CommentStyle::Any)
                   && c == QLatin1Char('/') && i + 1 < length && text.at(i + 1) == QLatin1Char('*')) {
            const int close = int(text.indexOf(QStringLiteral("*/"), i + 2));
            const int end = close < 0 ? length : close + 2;
            setFormat(i, end - i, m_commentFormat);
            i = end - 1;
        } else if (startsLineComment(text, i, style)) {
            setFormat(i, length - i, m_commentFormat);
            return;
        }
    }
    if (stringStart >= 0)
        setFormat(stringStart, length - stringStart, m_stringFormat);
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    int from = 0;
    while ((from = text.indexOf(m_searchQuery, from, Qt::CaseInsensitive)) >= 0) {
        const int documentStart = currentBlock().position() + from;
        QTextCharFormat format = this->format(from);
        format.setBackground(documentStart == m_currentMatchStart
                                 ? m_currentSearchFormat.background()
                                 : m_searchFormat.background());
        setFormat(from, m_searchQuery.length(), format);
        from += qMax(1, m_searchQuery.length());
    }
}

void MarkdownHighlighter::highlightMarkers(const QString &text) {
    int first = 0;
    while (first < text.length() && text.at(first).isSpace())
        ++first;
    if (first >= text.length())
        return;

    const QChar firstChar = text.at(first);
    if (first == 0 && firstChar == QLatin1Char('#')) {
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        const QRegularExpressionMatch heading = headingRe.match(text);
        if (heading.hasMatch()) {
            const int level = int(heading.capturedLength(1)) - 1;
            setFormat(0, heading.capturedLength(1) + heading.capturedLength(2),
                      m_headingMarkerFormats[level]);
            setFormat(heading.capturedStart(3), heading.capturedLength(3),
                      m_headingFormats[level]);
            return;
        }
    }

    if (firstChar == QLatin1Char('>')) {
        static const QRegularExpression quoteRe(QStringLiteral("^(\\s*>+\\s?)(.*)$"));
        const QRegularExpressionMatch quote = quoteRe.match(text);
        if (quote.hasMatch()) {
            setFormat(0, quote.capturedLength(1), m_markerFormat);
            setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('+')
            || firstChar == QLatin1Char('*') || firstChar.isDigit()) {
        static const QRegularExpression listRe(
            QStringLiteral("^(\\s*(?:[-+*]|\\d+[.)])\\s+)(.*)$"));
        const QRegularExpressionMatch list = listRe.match(text);
        if (list.hasMatch())
            setFormat(0, list.capturedLength(1), m_markerFormat);
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('*')
            || firstChar == QLatin1Char('_')) {
        static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
        const QRegularExpressionMatch rule = ruleRe.match(text);
        if (rule.hasMatch())
            setFormat(0, text.length(), m_markerFormat);
    }
}

void MarkdownHighlighter::highlightInline(const QString &text) {
    // Merge onto what is already there, so emphasis inside a heading keeps the
    // heading's size and color.
    const auto apply = [this](const Span &span, const QTextCharFormat &extra) {
        QTextCharFormat merged = format(span.start);
        merged.merge(extra);
        setFormat(span.start, span.length, merged);
    };

    for (const VerbatimSpan &verbatim : verbatimSpans(text))
        apply(verbatim.span, verbatim.math ? m_mathFormat : m_codeFormat);

    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        QTextCharFormat content =
            item.kind == InlineKind::Bold ? m_boldFormat
            : item.kind == InlineKind::Italic ? m_italicFormat
                                              : m_linkFormat;
        if (item.kind != InlineKind::Link
                && format(item.content.start).hasProperty(QTextFormat::FontPixelSize))
            content.clearForeground();
        apply(item.content, content);
        for (const Span &marker : item.markers)
            setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
    }
}

QList<MarkdownHighlighter::VerbatimSpan> MarkdownHighlighter::verbatimSpans(const QString &text) {
    QList<VerbatimSpan> spans;
    if (!text.contains(QLatin1Char('`')) && !text.contains(QLatin1Char('$')))
        return spans;

    QString masked = text;
    static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
    QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text);
    while (codeMatches.hasNext()) {
        const QRegularExpressionMatch match = codeMatches.next();
        const Span span{int(match.capturedStart()), int(match.capturedLength())};
        spans.append({span, false});
        for (int i = span.start; i < span.start + span.length; ++i)
            masked[i] = QLatin1Char(' ');
    }

    // $$display$$ or $inline$, but not prices: no space just inside the
    // dollars, and no digit right after the closing one.
    static const QRegularExpression mathRe(QStringLiteral(
        "\\$\\$(.+?)\\$\\$|(?<![\\\\$])\\$(?![\\s$])([^$\\n]*?[^\\s\\\\$])\\$(?![\\d$])"));
    QRegularExpressionMatchIterator mathMatches = mathRe.globalMatch(masked);
    while (mathMatches.hasNext()) {
        const QRegularExpressionMatch match = mathMatches.next();
        spans.append({{int(match.capturedStart()), int(match.capturedLength())}, true});
    }

    std::sort(spans.begin(), spans.end(), [](const VerbatimSpan &a, const VerbatimSpan &b) {
        return a.span.start < b.span.start;
    });
    return spans;
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &original) {
    QList<InlineMarkup> markup;
    if (!original.contains(QLatin1Char('*')) && !original.contains(QLatin1Char('_'))
            && !original.contains(QLatin1Char('['))) {
        return markup;
    }

    // Blank out code and math so their * _ [ never read as Markdown; the
    // positions found in the masked copy still match the original.
    QString text = original;
    for (const VerbatimSpan &verbatim : verbatimSpans(original)) {
        for (int i = verbatim.span.start; i < verbatim.span.start + verbatim.span.length; ++i)
            text[i] = QLatin1Char(' ');
    }

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };

    static const QRegularExpression boldRe(QStringLiteral("(\\*\\*|__)(.+?)(\\1)"));
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(text);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        markup.append({InlineKind::Bold, span(match, 2),
                       {span(match, 1), span(match, 3)}});
    }

    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*([^*\\n]+)\\*(?!\\*)|(?<!_)_([^_\\n]+)_(?!_)"));
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(text);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        markup.append({InlineKind::Italic, span(match, contentIndex),
                       {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
    }

    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^\\]]+)\\]\\(((?:\\\\.|[^)])+)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(text);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        markup.append({InlineKind::Link, content,
                       {{whole.start, 1},
                        {contentEnd, whole.start + whole.length - contentEnd}}});
    }

    // [[Target]] shows Target; [[Target|Shown text]] shows only the alias.
    static const QRegularExpression wikiLinkRe(
        QStringLiteral("\\[\\[([^\\[\\]\\n|]+?)(?:\\|([^\\[\\]\\n]+?))?\\]\\]"));
    QRegularExpressionMatchIterator wikiLinkMatches = wikiLinkRe.globalMatch(text);
    while (wikiLinkMatches.hasNext()) {
        const QRegularExpressionMatch match = wikiLinkMatches.next();
        const Span whole = span(match, 0);
        const Span content = match.capturedStart(2) >= 0 ? span(match, 2) : span(match, 1);
        markup.append({InlineKind::Link, content,
                       {{whole.start, content.start - whole.start},
                        {content.start + content.length, 2}}});
    }

    return markup;
}
