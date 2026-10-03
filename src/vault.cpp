#include "vault.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>

namespace {
const QString vaultPathSetting = QStringLiteral("vault/path");

const QRegularExpression &wikiLinkRe() {
    static const QRegularExpression re(QStringLiteral("\\[\\[([^\\[\\]\\n]+?)\\]\\]"));
    return re;
}

QString withoutMarkdownSuffix(const QString &name) {
    if (name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        return name.chopped(3);
    if (name.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive))
        return name.chopped(9);
    return name;
}

// A short excerpt of a line, starting near the first match so long
// paragraphs still show why they matched.
QString snippetAround(const QString &line, int column) {
    constexpr int maxLength = 140;
    int leading = 0;
    while (leading < line.length() && line.at(leading).isSpace())
        ++leading;
    const QString trimmed = line.trimmed();
    const int start = qMax(0, column - leading - 40);
    QString snippet = trimmed.mid(start, maxLength);
    if (start > 0)
        snippet.prepend(QStringLiteral("…"));
    if (start + maxLength < trimmed.length())
        snippet.append(QStringLiteral("…"));
    return snippet;
}
}

Vault::Vault(QObject *parent) : QObject(parent) {
    const QString saved = QSettings().value(vaultPathSetting).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        m_path = saved;
}

QString Vault::name() const {
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

bool Vault::ready() const {
    return !m_path.isEmpty() && QDir(m_path).exists();
}

void Vault::setPath(const QString &path) {
    const QString cleaned = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (cleaned == m_path)
        return;

    m_path = cleaned;
    m_notes.clear();
    m_textCache.clear();
    m_scanned = false;
    QSettings().setValue(vaultPathSetting, m_path);
    emit pathChanged();
}

void Vault::setFolder(const QUrl &folder) {
    if (folder.isLocalFile())
        setPath(folder.toLocalFile());
}

bool Vault::contains(const QUrl &fileUrl) const {
    if (!ready() || !fileUrl.isLocalFile())
        return false;
    const QString file = QDir::cleanPath(fileUrl.toLocalFile());
    return file.startsWith(m_path + QLatin1Char('/'));
}

void Vault::refresh() {
    m_scanned = false;
    ensureScanned();
}

void Vault::ensureScanned() {
    if (m_scanned)
        return;
    m_scanned = true;
    m_notes.clear();
    if (!ready())
        return;

    const QDir root(m_path);
    QDirIterator it(m_path, {QStringLiteral("*.md"), QStringLiteral("*.markdown")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString relative = root.relativeFilePath(info.filePath());
        // Skip tool folders such as .obsidian, .git and .trash.
        if (relative.startsWith(QLatin1Char('.')) || relative.contains(QStringLiteral("/.")))
            continue;
        m_notes.append({info.filePath(), withoutMarkdownSuffix(info.fileName()),
                        withoutMarkdownSuffix(relative), info.lastModified()});
    }

    std::sort(m_notes.begin(), m_notes.end(), [](const Note &a, const Note &b) {
        return a.modified > b.modified;
    });
}

QString Vault::noteText(const Note &note) {
    const auto cached = m_textCache.constFind(note.path);
    if (cached != m_textCache.constEnd() && cached->modified == note.modified)
        return cached->text;

    QFile file(note.path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString text = QString::fromUtf8(file.readAll());
    m_textCache.insert(note.path, {note.modified, text});
    return text;
}

QVariantMap Vault::noteEntry(const Note &note) const {
    const QString folder = note.relativeName.contains(QLatin1Char('/'))
        ? note.relativeName.section(QLatin1Char('/'), 0, -2)
        : QString();
    return {{QStringLiteral("url"), QUrl::fromLocalFile(note.path)},
            {QStringLiteral("title"), note.title},
            {QStringLiteral("folder"), folder}};
}

QVariantList Vault::findNotes(const QString &query, int limit) {
    ensureScanned();
    const QString trimmed = query.trimmed();

    QList<QPair<int, const Note *>> ranked;
    for (const Note &note : std::as_const(m_notes)) {
        if (trimmed.isEmpty()) {
            ranked.append({0, &note});
            continue;
        }
        // Titles count most; a folder name in the query can still find a note.
        const int score = qMax(fuzzyScore(trimmed, note.title),
                               fuzzyScore(trimmed, note.relativeName) - 10);
        if (score >= 0)
            ranked.append({score, &note});
    }

    // Stable, so ties (and the empty query) keep most-recently-modified first.
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });

    QVariantList results;
    for (const auto &entry : std::as_const(ranked)) {
        if (results.size() >= limit)
            break;
        results.append(noteEntry(*entry.second));
    }
    return results;
}

QVariantList Vault::searchText(const QString &query, int limit) {
    ensureScanned();
    const QStringList terms = query.toLower().split(QRegularExpression(QStringLiteral("\\s+")),
                                                    Qt::SkipEmptyParts);
    QVariantList results;
    if (terms.isEmpty())
        return results;

    for (const Note &note : std::as_const(m_notes)) {
        const QStringList lines = noteText(note).split(QLatin1Char('\n'));
        for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            const QString lower = lines.at(lineIndex).toLower();
            int firstColumn = -1;
            bool allTerms = true;
            for (const QString &term : terms) {
                const int column = lower.indexOf(term);
                if (column < 0) {
                    allTerms = false;
                    break;
                }
                firstColumn = firstColumn < 0 ? column : qMin(firstColumn, column);
            }
            if (!allTerms)
                continue;

            QVariantMap entry = noteEntry(note);
            entry.insert(QStringLiteral("line"), lineIndex);
            entry.insert(QStringLiteral("snippet"), snippetAround(lines.at(lineIndex), firstColumn));
            results.append(entry);
            if (results.size() >= limit)
                return results;
        }
    }
    return results;
}

QVariantList Vault::backlinks(const QUrl &noteUrl) {
    QVariantList results;
    if (!contains(noteUrl))
        return results;
    ensureScanned();

    const QString target = QDir::cleanPath(noteUrl.toLocalFile());
    const QString relative = withoutMarkdownSuffix(QDir(m_path).relativeFilePath(target)).toLower();
    const QString title = withoutMarkdownSuffix(QFileInfo(target).fileName()).toLower();

    for (const Note &note : std::as_const(m_notes)) {
        if (note.path == target)
            continue;
        const QString text = noteText(note);
        if (!text.contains(QStringLiteral("[[")))
            continue;

        const QStringList lines = text.split(QLatin1Char('\n'));
        for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            const QString &line = lines.at(lineIndex);
            QRegularExpressionMatchIterator matches = wikiLinkRe().globalMatch(line);
            while (matches.hasNext()) {
                const QRegularExpressionMatch match = matches.next();
                const QString linked = withoutMarkdownSuffix(linkTarget(match.captured(1))).toLower();
                if (linked != title && linked != relative)
                    continue;

                QVariantMap entry = noteEntry(note);
                entry.insert(QStringLiteral("line"), lineIndex);
                entry.insert(QStringLiteral("snippet"),
                             snippetAround(line, int(match.capturedStart(0))));
                results.append(entry);
                break;
            }
        }
    }
    return results;
}

int Vault::backlinkCount(const QUrl &noteUrl) {
    return int(backlinks(noteUrl).size());
}

QUrl Vault::resolveLink(const QString &target) {
    const QString wanted = withoutMarkdownSuffix(linkTarget(target)).toLower();
    if (wanted.isEmpty() || !ready())
        return {};

    // A note created since the last scan (say, in Finder) still resolves.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt == 1)
            refresh();
        else
            ensureScanned();

        const Note *best = nullptr;
        for (const Note &note : std::as_const(m_notes)) {
            if (note.relativeName.toLower() == wanted)
                return QUrl::fromLocalFile(note.path);
            if (note.title.toLower() == wanted
                    && (!best || note.relativeName.length() < best->relativeName.length()))
                best = &note;
        }
        if (best)
            return QUrl::fromLocalFile(best->path);
    }
    return {};
}

QUrl Vault::createNote(const QString &title) {
    if (!ready())
        return {};

    QString name = title.trimmed();
    static const QRegularExpression unsafe(QStringLiteral("[/\\\\:\\x00-\\x1f\\x7f]"));
    name.replace(unsafe, QStringLiteral("-"));
    name = withoutMarkdownSuffix(name).trimmed();
    if (name.isEmpty() || name.startsWith(QLatin1Char('.')))
        name = QStringLiteral("Untitled");

    const QUrl existing = resolveLink(name);
    if (existing.isValid())
        return existing;

    const QString path = QDir(m_path).filePath(name + QStringLiteral(".md"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text))
        return {};
    file.write(QStringLiteral("# %1\n\n").arg(name).toUtf8());
    file.close();

    refresh();
    return QUrl::fromLocalFile(path);
}

QString Vault::linkAt(const QString &text, int position) const {
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1)) + 1;
    int lineEnd = text.indexOf(QLatin1Char('\n'), position);
    if (lineEnd < 0)
        lineEnd = int(text.length());

    const QString line = text.mid(lineStart, lineEnd - lineStart);
    const int column = position - lineStart;
    QRegularExpressionMatchIterator matches = wikiLinkRe().globalMatch(line);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (column >= match.capturedStart(0) && column <= match.capturedEnd(0))
            return linkTarget(match.captured(1));
    }
    return {};
}

// Subsequence match, scored so that consecutive characters, word starts and
// whole-substring hits rank first, and shorter candidates win ties.
// Returns -1 when the query's characters do not all appear in order.
int Vault::fuzzyScore(const QString &query, const QString &candidate) {
    const QString q = query.toLower().remove(QLatin1Char(' '));
    const QString c = candidate.toLower();
    if (q.isEmpty())
        return 0;

    int score = 0;
    int queryIndex = 0;
    int previous = -2;
    for (int i = 0; i < c.length() && queryIndex < q.length(); ++i) {
        if (c.at(i) != q.at(queryIndex))
            continue;
        int bonus = 1;
        if (i == previous + 1)
            bonus += 5;
        if (i == 0 || !c.at(i - 1).isLetterOrNumber())
            bonus += 8;
        score += bonus;
        previous = i;
        ++queryIndex;
    }
    if (queryIndex < q.length())
        return -1;

    // Whole-query hits only count from a word start, so "pk" still prefers
    // "personal knowledge" over "upkeep".
    const QString whole = query.trimmed().toLower();
    const int hit = int(c.indexOf(whole));
    if (c == whole)
        score += 100;
    else if (hit == 0)
        score += 30;
    else if (hit > 0 && !c.at(hit - 1).isLetterOrNumber())
        score += 15;
    return qMax(0, score - int(c.length()) / 8);
}

// [[Target|Shown text]] and [[Target#Heading]] both link to Target.
QString Vault::linkTarget(const QString &linkBody) {
    return linkBody.section(QLatin1Char('|'), 0, 0).section(QLatin1Char('#'), 0, 0).trimmed();
}

QStringList Vault::wikiLinkTargets(const QString &text) {
    QStringList targets;
    QRegularExpressionMatchIterator matches = wikiLinkRe().globalMatch(text);
    while (matches.hasNext()) {
        const QString target = linkTarget(matches.next().captured(1));
        if (!target.isEmpty())
            targets.append(target);
    }
    return targets;
}
