#include "spellchecker.h"

// No system spell checker is wired up outside macOS yet.

SpellChecker::SpellChecker() = default;
SpellChecker::~SpellChecker() = default;

bool SpellChecker::available() {
    return false;
}

bool SpellChecker::systemAutocorrectEnabled() {
    return false;
}

QList<SpellChecker::Range> SpellChecker::misspellings(const QString &) {
    return {};
}

bool SpellChecker::isMisspelled(const QString &) {
    return false;
}

QStringList SpellChecker::guesses(const QString &) {
    return {};
}

QString SpellChecker::correction(const QString &) {
    return {};
}

QString SpellChecker::replacement(const QString &) const {
    return {};
}

void SpellChecker::learn(const QString &) {}

void SpellChecker::ignore(const QString &) {}

void SpellChecker::lookUp(QWindow *, const QString &, const QPointF &, qreal) {}
