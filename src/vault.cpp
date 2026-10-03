#include "vault.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>

namespace {
const QString vaultPathSetting = QStringLiteral("vault/path");
const QString expandedSetting = QStringLiteral("vault/expanded");

bool isNoteFile(const QString &name) {
    return name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)
        || name.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive);
}

const QStringList imageSuffixes = {
    QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
    QStringLiteral("svg"), QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("tiff"),
    QStringLiteral("heic")};

bool isImageFile(const QString &name) {
    return imageSuffixes.contains(QFileInfo(name).suffix().toLower());
}

// ![[target]] / ![[target|300]] and ![alt](target), outside code.
const QRegularExpression &imageRe() {
    static const QRegularExpression re(
        QStringLiteral("!\\[\\[([^\\[\\]\\n]+?)\\]\\]|!\\[[^\\]\\n]*\\]\\(([^)\\n]+)\\)"));
    return re;
}

QString cleanName(QString name) {
    static const QRegularExpression unsafe(QStringLiteral("[/\\\\:\\x00-\\x1f\\x7f]"));
    name.replace(unsafe, QStringLiteral("-"));
    name = name.trimmed();
    return name.startsWith(QLatin1Char('.')) ? QString() : name;
}

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
    m_expanded = QSettings().value(expandedSetting).toStringList();
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
    m_expanded.clear();
    QSettings().setValue(expandedSetting, m_expanded);
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
    m_imagesScanned = false;
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

QString Vault::absolute(const QString &relativePath) const {
    return relativePath.isEmpty() ? m_path : QDir(m_path).filePath(relativePath);
}

QString Vault::relativePath(const QUrl &url) const {
    if (!contains(url))
        return {};
    return QDir(m_path).relativeFilePath(QDir::cleanPath(url.toLocalFile()));
}

QUrl Vault::urlFor(const QString &relativePath) const {
    return QUrl::fromLocalFile(absolute(relativePath));
}

void Vault::changed() {
    m_textCache.clear();
    refresh();
    emit treeChanged();
}

QVariantList Vault::tree() {
    QVariantList rows;
    if (ready())
        appendTree(QString(), 0, &rows);
    return rows;
}

void Vault::appendTree(const QString &folder, int depth, QVariantList *rows) const {
    const QFileInfoList entries = QDir(absolute(folder)).entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        const bool isFolder = entry.isDir();
        const bool image = !isFolder && isImageFile(entry.fileName());
        if (!isFolder && !image && !isNoteFile(entry.fileName()))
            continue;
        const QString path = folder.isEmpty() ? entry.fileName()
                                              : folder + QLatin1Char('/') + entry.fileName();
        const bool expanded = isFolder && m_expanded.contains(path);
        rows->append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("name"), isFolder || image ? entry.fileName()
                                                       : withoutMarkdownSuffix(entry.fileName())},
            {QStringLiteral("depth"), depth},
            {QStringLiteral("folder"), isFolder},
            {QStringLiteral("image"), image},
            {QStringLiteral("expanded"), expanded}});
        if (expanded)
            appendTree(path, depth + 1, rows);
    }
}

void Vault::setExpanded(const QString &folder, bool expanded) {
    if (expanded == m_expanded.contains(folder))
        return;
    if (expanded)
        m_expanded.append(folder);
    else
        m_expanded.removeAll(folder);
    QSettings().setValue(expandedSetting, m_expanded);
    emit treeChanged();
}

// Opens every folder above a note, so the sidebar can show where it lives.
void Vault::reveal(const QUrl &noteUrl) {
    const QString path = relativePath(noteUrl);
    bool changedAny = false;
    for (int slash = int(path.indexOf(QLatin1Char('/'))); slash > 0;
         slash = int(path.indexOf(QLatin1Char('/'), slash + 1))) {
        const QString folder = path.left(slash);
        if (!m_expanded.contains(folder)) {
            m_expanded.append(folder);
            changedAny = true;
        }
    }
    if (changedAny) {
        QSettings().setValue(expandedSetting, m_expanded);
        emit treeChanged();
    }
}

QVariantList Vault::findFolders(const QString &query, int limit) {
    QVariantList results;
    if (!ready())
        return results;
    QList<QPair<int, QString>> ranked{{0, QString()}};
    const QDir root(m_path);
    QDirIterator it(m_path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString relative = root.relativeFilePath(it.next());
        if (relative.startsWith(QLatin1Char('.')) || relative.contains(QStringLiteral("/.")))
            continue;
        ranked.append({0, relative});
    }
    const QString trimmed = query.trimmed();
    QList<QPair<int, QString>> matches;
    for (const auto &folder : std::as_const(ranked)) {
        const QString shown = folder.second.isEmpty() ? name() : folder.second;
        const int score = trimmed.isEmpty() ? 0 : fuzzyScore(trimmed, shown);
        if (score >= 0)
            matches.append({score, folder.second});
    }
    std::stable_sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    for (const auto &match : std::as_const(matches)) {
        if (results.size() >= limit)
            break;
        results.append(QVariantMap{{QStringLiteral("path"), match.second},
                                   {QStringLiteral("title"), match.second.isEmpty()
                                        ? QStringLiteral("/ (vault root)") : match.second},
                                   {QStringLiteral("folder"), QString()}});
    }
    return results;
}

QString Vault::uniqueName(const QString &folder, const QString &base, const QString &suffix) const {
    const QDir directory(absolute(folder));
    QString candidate = base + suffix;
    for (int n = 2; directory.exists(candidate); ++n)
        candidate = QStringLiteral("%1 %2%3").arg(base).arg(n).arg(suffix);
    return candidate;
}

QString Vault::createNoteIn(const QString &folder) {
    if (!ready())
        return {};
    const QString fileName = uniqueName(folder, QStringLiteral("Untitled"), QStringLiteral(".md"));
    QFile file(QDir(absolute(folder)).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text))
        return {};
    file.close();
    if (!folder.isEmpty())
        setExpanded(folder, true);
    changed();
    return folder.isEmpty() ? fileName : folder + QLatin1Char('/') + fileName;
}

QString Vault::createFolder(const QString &parent) {
    if (!ready())
        return {};
    const QString folderName = uniqueName(parent, QStringLiteral("New Folder"), QString());
    if (!QDir(absolute(parent)).mkdir(folderName))
        return {};
    if (!parent.isEmpty())
        setExpanded(parent, true);
    changed();
    return parent.isEmpty() ? folderName : parent + QLatin1Char('/') + folderName;
}

// [[Old]], [[Old|alias]] and [[Old#Heading]] become [[New...]]; the alias and
// heading are kept, and links to anything else are left untouched.
QString Vault::renameLinks(const QString &text, const QStringList &oldTargets,
                           const QString &newTarget) {
    QStringList escaped;
    for (const QString &target : oldTargets)
        escaped.append(QRegularExpression::escape(target));
    const QRegularExpression linkRe(
        QStringLiteral("\\[\\[\\s*(?:%1)\\s*((?:#[^\\]|\\n]*)?(?:\\|[^\\]\\n]*)?)\\]\\]")
            .arg(escaped.join(QLatin1Char('|'))),
        QRegularExpression::CaseInsensitiveOption);
    QString result = text;
    result.replace(linkRe, QStringLiteral("[[%1\\1]]").arg(newTarget));
    return result;
}

QVariantMap Vault::rename(const QString &path, const QString &newName) {
    const QFileInfo info(absolute(path));
    const QString name = cleanName(withoutMarkdownSuffix(newName));
    if (!ready() || path.isEmpty() || !info.exists() || name.isEmpty())
        return {{QStringLiteral("error"), QStringLiteral("That name can't be used.")}};

    const bool isFolder = info.isDir();
    const bool image = !isFolder && isImageFile(info.fileName());
    const QString imageName = image && QFileInfo(name).suffix().toLower() != info.suffix().toLower()
        ? cleanName(newName) + QLatin1Char('.') + info.suffix()
        : cleanName(newName);
    const QString fileName = isFolder ? name : image ? imageName : name + QStringLiteral(".md");
    const QString parent = path.contains(QLatin1Char('/')) ? path.section(QLatin1Char('/'), 0, -2)
                                                          : QString();
    const QString newPath = parent.isEmpty() ? fileName : parent + QLatin1Char('/') + fileName;
    const QString target = absolute(newPath);
    if (target == info.absoluteFilePath())
        return {{QStringLiteral("path"), path}, {QStringLiteral("updatedNotes"), 0}};

    // A case-only rename on a case-insensitive disk goes through a temporary name.
    const bool sameFile = QFileInfo::exists(target)
        && QFileInfo(target).canonicalFilePath() == info.canonicalFilePath();
    if (QFileInfo::exists(target) && !sameFile)
        return {{QStringLiteral("error"), QStringLiteral("%1 already exists.").arg(fileName)}};
    QDir directory(info.absolutePath());
    if (sameFile) {
        const QString temporary = uniqueName(parent, QStringLiteral(".omawrite-rename"), QString());
        if (!directory.rename(info.fileName(), temporary) || !directory.rename(temporary, fileName))
            return {{QStringLiteral("error"), QStringLiteral("Could not rename.")}};
    } else if (!directory.rename(info.fileName(), fileName)) {
        return {{QStringLiteral("error"), QStringLiteral("Could not rename.")}};
    }

    // A note just created in the tree is empty; naming it gives it its heading.
    if (!isFolder && !image && info.size() == 0) {
        QFile file(target);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            file.write(QStringLiteral("# %1\n\n").arg(name).toUtf8());
    }

    if (isFolder) {
        for (QString &expanded : m_expanded) {
            if (expanded == path || expanded.startsWith(path + QLatin1Char('/')))
                expanded = newPath + expanded.mid(path.length());
        }
        QSettings().setValue(expandedSetting, m_expanded);
    }

    // Notes are linked by title and images by file name, so renaming either
    // takes the references to it along.
    int updated = 0;
    if (!isFolder) {
        refresh();
        const QString oldTitle = withoutMarkdownSuffix(info.fileName());
        const QStringList oldTargets{oldTitle, withoutMarkdownSuffix(path)};
        for (const Note &note : std::as_const(m_notes)) {
            QFile file(note.path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QString text = QString::fromUtf8(file.readAll());
            file.close();
            if (!text.contains(QStringLiteral("[[")))
                continue;
            const QString renamed = image ? renameImageLinks(text, info.fileName(), fileName)
                                          : renameLinks(text, oldTargets, name);
            if (renamed == text)
                continue;
            QSaveFile save(note.path);
            if (save.open(QIODevice::WriteOnly | QIODevice::Text)) {
                save.write(renamed.toUtf8());
                if (save.commit())
                    ++updated;
            }
        }
    }

    changed();
    return {{QStringLiteral("path"), newPath}, {QStringLiteral("updatedNotes"), updated}};
}

QVariantMap Vault::move(const QString &path, const QString &folder) {
    const QFileInfo info(absolute(path));
    if (!ready() || path.isEmpty() || !info.exists())
        return {{QStringLiteral("error"), QStringLiteral("Nothing to move.")}};
    if (info.isDir() && (folder == path || folder.startsWith(path + QLatin1Char('/'))))
        return {{QStringLiteral("error"), QStringLiteral("A folder can't move into itself.")}};

    const QString newPath = folder.isEmpty() ? info.fileName()
                                             : folder + QLatin1Char('/') + info.fileName();
    if (newPath == path)
        return {{QStringLiteral("path"), path}};
    if (QFileInfo::exists(absolute(newPath)))
        return {{QStringLiteral("error"), QStringLiteral("%1 already exists there.").arg(info.fileName())}};
    if (!QDir().rename(info.absoluteFilePath(), absolute(newPath)))
        return {{QStringLiteral("error"), QStringLiteral("Could not move.")}};

    if (!folder.isEmpty())
        setExpanded(folder, true);
    changed();
    return {{QStringLiteral("path"), newPath}};
}

bool Vault::trash(const QString &path) {
    if (!ready() || path.isEmpty() || !QFile::moveToTrash(absolute(path)))
        return false;
    m_expanded.removeAll(path);
    changed();
    return true;
}

// Notes and folders anywhere in the vault matching a fuzzy query, as tree
// rows with their location, best matches first.
QVariantList Vault::findEntries(const QString &query, int limit) {
    QVariantList results;
    const QString trimmed = query.trimmed();
    if (!ready() || trimmed.isEmpty())
        return results;

    const QDir root(m_path);
    QList<QPair<int, QVariantMap>> ranked;
    QDirIterator it(m_path, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString path = root.relativeFilePath(info.filePath());
        if (path.startsWith(QLatin1Char('.')) || path.contains(QStringLiteral("/."))
                || (!info.isDir() && !isNoteFile(info.fileName()) && !isImageFile(info.fileName())))
            continue;
        const bool image = !info.isDir() && isImageFile(info.fileName());
        const QString name = info.isDir() || image ? info.fileName()
                                                   : withoutMarkdownSuffix(info.fileName());
        const int score = qMax(fuzzyScore(trimmed, name), fuzzyScore(trimmed, path) - 10);
        if (score < 0)
            continue;
        const QString location = path.contains(QLatin1Char('/')) ? path.section(QLatin1Char('/'), 0, -2)
                                                                : QString();
        ranked.append({score, QVariantMap{{QStringLiteral("path"), path},
                                          {QStringLiteral("name"), name},
                                          {QStringLiteral("depth"), 0},
                                          {QStringLiteral("folder"), info.isDir()},
                                          {QStringLiteral("image"), image},
                                          {QStringLiteral("expanded"), false},
                                          {QStringLiteral("location"), location}}});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    for (const auto &entry : std::as_const(ranked)) {
        if (results.size() >= limit)
            break;
        results.append(entry.second);
    }
    return results;
}

void Vault::revealPath(const QString &path) {
    reveal(urlFor(path));
}

void Vault::ensureImagesScanned() {
    if (m_imagesScanned)
        return;
    m_imagesScanned = true;
    m_imagesByName.clear();
    if (!ready())
        return;
    QDirIterator it(m_path, QDir::Files, QDirIterator::Subdirectories);
    const QDir root(m_path);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString relative = root.relativeFilePath(info.filePath());
        if (!isImageFile(info.fileName()) || relative.startsWith(QLatin1Char('.'))
                || relative.contains(QStringLiteral("/.")))
            continue;
        const QString key = info.fileName().toLower();
        // Prefer the attachments folder, then the shortest path.
        const QString existing = m_imagesByName.value(key);
        const bool inAssets = relative.startsWith(QStringLiteral("assets/"));
        if (existing.isEmpty() || (inAssets && !existing.contains(QStringLiteral("/assets/")))
                || (!inAssets == !existing.contains(QStringLiteral("/assets/"))
                    && info.filePath().length() < existing.length()))
            m_imagesByName.insert(key, info.filePath());
    }
}

// The file an image reference points at: [[name|300]] loses its size, and
// %20-style escapes in Markdown links are undone.
QString Vault::imageTarget(const QString &reference) {
    QString target = reference.section(QLatin1Char('|'), 0, 0).section(QLatin1Char('#'), 0, 0).trimmed();
    if (target.startsWith(QLatin1Char('<')) && target.endsWith(QLatin1Char('>')))
        target = target.mid(1, target.length() - 2);
    // A Markdown link may carry a title: ![alt](file.png "Title").
    static const QRegularExpression titleRe(QStringLiteral("\\s+\"[^\"]*\"$"));
    target.remove(titleRe);
    return QUrl::fromPercentEncoding(target.toUtf8());
}

QUrl Vault::resolveImage(const QString &target, const QUrl &noteUrl) {
    const QString name = imageTarget(target);
    if (name.isEmpty())
        return {};
    if (name.startsWith(QStringLiteral("http://")) || name.startsWith(QStringLiteral("https://")))
        return QUrl(name);
    if (name.startsWith(QStringLiteral("file://")))
        return QUrl(name);

    QStringList candidates;
    if (QFileInfo(name).isAbsolute())
        candidates.append(name);
    if (noteUrl.isLocalFile())
        candidates.append(QFileInfo(noteUrl.toLocalFile()).dir().filePath(name));
    if (ready())
        candidates.append(QDir(m_path).filePath(name));
    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo(candidate).isFile())
            return QUrl::fromLocalFile(QDir::cleanPath(candidate));
    }

    ensureImagesScanned();
    const QString found = m_imagesByName.value(QFileInfo(name).fileName().toLower());
    return found.isEmpty() ? QUrl() : QUrl::fromLocalFile(found);
}

QString Vault::imageAt(const QString &text, int position) const {
    const int lineStart = int(text.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1))) + 1;
    int lineEnd = int(text.indexOf(QLatin1Char('\n'), position));
    if (lineEnd < 0)
        lineEnd = int(text.length());
    const QString line = text.mid(lineStart, lineEnd - lineStart);
    QRegularExpressionMatchIterator matches = imageRe().globalMatch(line);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (position - lineStart >= match.capturedStart() && position - lineStart <= match.capturedEnd())
            return match.capturedStart(1) >= 0 ? match.captured(1) : match.captured(2);
    }
    return {};
}

// The first image on the caret's line, and where that line ends.
QVariantMap Vault::imageOnLine(const QString &text, int position) const {
    const int lineStart = int(text.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1))) + 1;
    int lineEnd = int(text.indexOf(QLatin1Char('\n'), position));
    if (lineEnd < 0)
        lineEnd = int(text.length());
    const QRegularExpressionMatch match = imageRe().match(text.mid(lineStart, lineEnd - lineStart));
    if (!match.hasMatch())
        return {};
    return {{QStringLiteral("target"), match.capturedStart(1) >= 0 ? match.captured(1) : match.captured(2)},
            {QStringLiteral("lineEnd"), lineEnd}};
}

bool Vault::isImage(const QUrl &file) {
    return file.isLocalFile() && isImageFile(file.toLocalFile());
}

// assets/ at the vault root, where pasted and dropped images go.
QString Vault::attachmentsFolder() {
    if (!ready())
        return {};
    const QString folder = QDir(m_path).filePath(QStringLiteral("assets"));
    QDir().mkpath(folder);
    return folder;
}

bool Vault::clipboardHasImage() const {
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    return data && data->hasImage() && !data->hasText();
}

// Saves the clipboard's image into assets/ the way Obsidian names it and
// returns the file name for an ![[...]] embed, or nothing.
QString Vault::pasteImage() {
    const QString folder = attachmentsFolder();
    const QImage image = qvariant_cast<QImage>(QGuiApplication::clipboard()->mimeData()->imageData());
    if (folder.isEmpty() || image.isNull())
        return {};
    const QString base = QStringLiteral("Pasted image %1")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss")));
    const QString fileName = uniqueName(QStringLiteral("assets"), base, QStringLiteral(".png"));
    if (!image.save(QDir(folder).filePath(fileName), "PNG"))
        return {};
    m_imagesScanned = false;
    emit treeChanged();
    return fileName;
}

// A dropped image already in the vault is linked where it is; anything else
// is copied into assets/ first. Returns the name to embed.
QString Vault::importImage(const QUrl &file) {
    if (!isImage(file))
        return {};
    const QString source = QDir::cleanPath(file.toLocalFile());
    if (contains(file))
        return QFileInfo(source).fileName();
    const QString folder = attachmentsFolder();
    if (folder.isEmpty())
        return {};
    const QFileInfo info(source);
    const QString fileName = uniqueName(QStringLiteral("assets"), info.completeBaseName(),
                                        QLatin1Char('.') + info.suffix());
    if (!QFile::copy(source, QDir(folder).filePath(fileName)))
        return {};
    m_imagesScanned = false;
    emit treeChanged();
    return fileName;
}

// ![[old.png]], ![[old.png|300]] and ![alt](old.png) or (assets/old.png),
// written plainly or %20-escaped, follow a renamed image.
QString Vault::renameImageLinks(const QString &text, const QString &oldName, const QString &newName) {
    QString result = text;
    const QRegularExpression embedRe(
        QStringLiteral("(\\[\\[\\s*(?:[^\\]|#\\n]*/)?)%1(\\s*(?:[|#][^\\]\\n]*)?\\]\\])")
            .arg(QRegularExpression::escape(oldName)),
        QRegularExpression::CaseInsensitiveOption);
    result.replace(embedRe, QStringLiteral("\\1%1\\2").arg(newName));
    const QString encodedOld = QString::fromUtf8(QUrl::toPercentEncoding(oldName));
    const QString encodedNew = QString::fromUtf8(QUrl::toPercentEncoding(newName));
    for (const auto &[from, to] : {std::pair{oldName, newName}, std::pair{encodedOld, encodedNew}}) {
        const QRegularExpression linkRe(
            QStringLiteral("(\\]\\(<?(?:[^)\\s]*/)?)%1(>?(?:\\s+\"[^\"]*\")?\\))").arg(QRegularExpression::escape(from)),
            QRegularExpression::CaseInsensitiveOption);
        result.replace(linkRe, QStringLiteral("\\1%1\\2").arg(to));
    }
    return result;
}
