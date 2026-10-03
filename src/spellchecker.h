#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

class QWindow;

// The system spell checker: NSSpellChecker on macOS, which brings the user's
// languages, learned words, autocorrection and text replacements with it.
// Elsewhere every method is a harmless no-op.
class SpellChecker {
public:
    struct Range {
        int start;
        int length;
    };

    SpellChecker();
    ~SpellChecker();

    static bool available();
    static bool systemAutocorrectEnabled();

    QList<Range> misspellings(const QString &text);
    bool isMisspelled(const QString &word);
    QStringList guesses(const QString &word);
    QString correction(const QString &word);
    QString replacement(const QString &word) const;
    void learn(const QString &word);
    void ignore(const QString &word);

    // Shows the system dictionary popover for text whose baseline starts at
    // the given point in window coordinates.
    static void lookUp(QWindow *window, const QString &text, const QPointF &baseline,
                       qreal pointSize);

private:
    long m_tag = 0;
    QHash<QString, QList<Range>> m_cache;
};
