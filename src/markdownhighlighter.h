#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include "themes.h"

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setDarkMode(bool darkMode);
    void setPalette(const ThemePalette &palette);
    void setSearch(const QString &query, int currentMatchStart);
    void refreshFont();

    struct Span {
        int start;
        int length;
    };

    enum class InlineKind { Bold, Italic, Link };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
    };

    // Inline `code` and $math$ are verbatim: no Markdown applies inside them.
    struct VerbatimSpan {
        Span span;
        bool math;
    };

    // Block states carried from line to line. Lines inside a fenced code block
    // or a $$ math block are verbatim as a whole.
    enum BlockState { NormalState = 0, MathState = 1, CodeState = 0x100 };
    enum class CommentStyle { Any, Hash, Slash, Dash, None };

    static bool isVerbatimState(int state);
    static CommentStyle commentStyleFor(const QString &language);

    // Single source of truth for inline markdown spans: the highlighter uses it
    // to style content and hide markers, and the editor uses it (via
    // Backend::hiddenRangesAt) to skip the caret over the hidden markers.
    static QList<InlineMarkup> inlineMarkup(const QString &text);
    static QList<VerbatimSpan> verbatimSpans(const QString &text);

    // The LaTeX under a caret position, without its dollar signs, and the
    // document range it came from; empty when the caret is not in math.
    struct MathSource {
        QString latex;
        int start = -1;
        int end = -1;
        bool display = false;
    };
    static MathSource mathAt(const QString &text, int position);
    static QString proseText(const QString &text);

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats();
    bool highlightFencedCode(const QString &text);
    bool highlightMathBlock(const QString &text);
    void highlightCodeLine(const QString &text, CommentStyle style);
    void highlightMarkers(const QString &text);
    void highlightInline(const QString &text);
    void highlightSearch(const QString &text);

    bool m_darkMode = true;
    ThemePalette m_palette;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_fenceFormat;
    QTextCharFormat m_mathFormat;
    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_linkFormat;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
};
