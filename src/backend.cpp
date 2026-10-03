#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>
#include <cmath>

#include "markdownhighlighter.h"
#include "preview.h"
#ifdef Q_OS_MACOS
#include "macintegration.h"
#endif
#include "spellchecker.h"

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");
const QString editorFontSetting = QStringLiteral("editor/font");
const QString textZoomSetting = QStringLiteral("view/textZoom");
const QString interfaceZoomSetting = QStringLiteral("view/interfaceZoom");
const QString themeSetting = QStringLiteral("view/theme");
const QString wrapModeSetting = QStringLiteral("view/wrap");
const QString spellCheckSetting = QStringLiteral("spelling/check");
const QString autocorrectSetting = QStringLiteral("spelling/autocorrect");
const QString vimModeSetting = QStringLiteral("editor/vim");

bool isWordCharacter(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('\'') || c == QChar(0x2019);
}
const QStringList wrapModes = {QStringLiteral("column"), QStringLiteral("window"),
                               QStringLiteral("off")};

// Noto ships a separate family per script, which buries every other font in
// the picker. Offer only the core Latin families; the rest still serve as
// fallbacks for whatever scripts a document contains.
const QStringList keptNotoFamilies = {QStringLiteral("Noto Sans"),
                                      QStringLiteral("Noto Sans Mono"),
                                      QStringLiteral("Noto Serif")};

// Fontconfig's generic aliases just point at another listed family.
const QStringList genericFontAliases = {QStringLiteral("Monospace"), QStringLiteral("Mono"),
                                        QStringLiteral("Sans Serif"), QStringLiteral("Sans"),
                                        QStringLiteral("Serif"), QStringLiteral("System-ui")};

// Symbol fonts often claim Latin coverage because they map their glyphs onto
// ASCII, so they can only be told apart by name. D050000L is URW's Dingbats.
const QRegularExpression symbolFontName(
    QStringLiteral("symbol|dingbat|emoji|icon|awesome|^D050000L$"),
    QRegularExpression::CaseInsensitiveOption);

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent) : QObject(parent) {
    const QString savedFont = QSettings().value(editorFontSetting).toString();
    m_editorFont = !savedFont.isEmpty() && QFontDatabase::hasFamily(savedFont)
        ? savedFont
        : defaultEditorFont();
    m_textZoom = steppedZoom(QSettings().value(textZoomSetting, 1.0).toReal(), 0);
    m_interfaceZoom = steppedZoom(QSettings().value(interfaceZoomSetting, 1.0).toReal(), 0);
    const QString savedTheme = QSettings().value(themeSetting).toString();
    m_theme = Themes::exists(savedTheme) ? savedTheme : Themes::defaultId();
    const QString savedWrap = QSettings().value(wrapModeSetting).toString();
    m_wrapMode = wrapModes.contains(savedWrap) ? savedWrap : wrapModes.constFirst();
    m_spellChecker = std::make_unique<SpellChecker>();
    m_vimMode = QSettings().value(vimModeSetting, false).toBool();
    m_spellCheckEnabled = QSettings().value(spellCheckSetting, true).toBool();
    m_autocorrectEnabled = QSettings().value(autocorrectSetting,
                                             SpellChecker::systemAutocorrectEnabled()).toBool();

    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDirectory);
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(120);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                const bool deleted = !QFileInfo::exists(path);
                if (!deleted && m_hasKnownFileContents) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)
                            && file.readAll() == m_lastKnownFileContents) {
                        // Atomic saves can replace the watched inode. Re-arm the
                        // watcher, but do not report our own save as an outside edit.
                        watchCurrentFile();
                        return;
                    }
                }

                emit externalChangeDetected(deleted, m_modified);
            });

    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::setTheme(const QString &theme) {
    if (theme == m_theme || !Themes::exists(theme))
        return;
    m_theme = theme;
    QSettings().setValue(themeSetting, theme);
    loadOmarchyTheme();
    emit themeChanged();
}

// Each entry carries its colors for the current light/dark mode, so the theme
// picker can draw a swatch without asking again.
QVariantList Backend::themes() const {
    QVariantList list;
    for (const ThemeInfo &info : Themes::all()) {
        const ThemePalette palette = Themes::palette(info.id, m_darkMode);
        list.append(QVariantMap{{QStringLiteral("id"), info.id},
                                {QStringLiteral("name"), info.name},
                                {QStringLiteral("background"), palette.background},
                                {QStringLiteral("foreground"), palette.foreground},
                                {QStringLiteral("heading"), palette.heading},
                                {QStringLiteral("accent"), palette.accent}});
    }
    return list;
}

void Backend::setWrapMode(const QString &mode) {
    if (mode == m_wrapMode || !wrapModes.contains(mode))
        return;
    m_wrapMode = mode;
    QSettings().setValue(wrapModeSetting, mode);
    emit wrapModeChanged();
}

void Backend::cycleWrapMode() {
    const int next = (wrapModes.indexOf(m_wrapMode) + 1) % wrapModes.size();
    setWrapMode(wrapModes.at(next));
    static const QStringList labels = {QStringLiteral("Wrap: column"),
                                       QStringLiteral("Wrap: window width"),
                                       QStringLiteral("Wrap: off")};
    setStatus(labels.at(next));
}

void Backend::setVimMode(bool enabled) {
    if (enabled == m_vimMode)
        return;
    m_vimMode = enabled;
    QSettings().setValue(vimModeSetting, enabled);
    setStatus(enabled ? QStringLiteral("Vim mode on") : QStringLiteral("Vim mode off"));
    emit vimModeChanged();
}

bool Backend::spellCheckAvailable() const {
    return SpellChecker::available();
}

void Backend::setSpellCheckEnabled(bool enabled) {
    if (enabled == m_spellCheckEnabled)
        return;
    m_spellCheckEnabled = enabled;
    QSettings().setValue(spellCheckSetting, enabled);
    emit spellCheckChanged();
    emit spellingChanged();
}

void Backend::setAutocorrectEnabled(bool enabled) {
    if (enabled == m_autocorrectEnabled)
        return;
    m_autocorrectEnabled = enabled;
    QSettings().setValue(autocorrectSetting, enabled);
    emit spellCheckChanged();
}

QVariantMap Backend::wordAt(const QString &text, int position) const {
    int start = qBound(0, position, int(text.length()));
    int end = start;
    while (start > 0 && isWordCharacter(text.at(start - 1)))
        --start;
    while (end < text.length() && isWordCharacter(text.at(end)))
        ++end;
    // Apostrophes belong inside words, not around them.
    while (start < end && !text.at(start).isLetterOrNumber())
        ++start;
    while (end > start && !text.at(end - 1).isLetterOrNumber())
        --end;
    return {{QStringLiteral("start"), start}, {QStringLiteral("end"), end},
            {QStringLiteral("word"), text.mid(start, end - start)}};
}

bool Backend::isMisspelled(const QString &word) {
    return m_spellCheckEnabled && !word.isEmpty() && m_spellChecker->isMisspelled(word);
}

QStringList Backend::spellingGuesses(const QString &word) {
    return word.isEmpty() ? QStringList() : m_spellChecker->guesses(word).mid(0, 6);
}

void Backend::learnWord(const QString &word) {
    m_spellChecker->learn(word);
    emit spellingChanged();
}

void Backend::ignoreWord(const QString &word) {
    m_spellChecker->ignore(word);
    emit spellingChanged();
}

// Misspelled prose words in a document range (the visible part of the page),
// for the editor to mark. The word under the caret is left alone while it is
// still being typed.
QVariantList Backend::misspellingsBetween(int start, int end) {
    QVariantList ranges;
    if (!m_document || !m_spellCheckEnabled || !SpellChecker::available())
        return ranges;

    for (QTextBlock block = m_document->findBlock(qMax(0, start));
         block.isValid() && block.position() <= end; block = block.next()) {
        if (MarkdownHighlighter::isVerbatimState(block.userState())
                || block.previous().userState() == MarkdownHighlighter::MathState)
            continue;
        const QString line = block.text();
        for (const SpellChecker::Range &range :
                 m_spellChecker->misspellings(MarkdownHighlighter::proseText(line))) {
            const int wordStart = block.position() + range.start;
            const int wordEnd = wordStart + range.length;
            if (m_caretPosition >= wordStart && m_caretPosition <= wordEnd)
                continue;
            ranges.append(QVariantMap{{QStringLiteral("start"), wordStart},
                                      {QStringLiteral("end"), wordEnd}});
        }
    }
    return ranges;
}

// Text replacements and autocorrection only touch prose: never code, math,
// link targets or URLs.
bool Backend::isProseAt(int position) const {
    if (!m_document)
        return false;
    const QTextBlock block = m_document->findBlock(position);
    if (!block.isValid() || MarkdownHighlighter::isVerbatimState(block.userState())
            || block.previous().userState() == MarkdownHighlighter::MathState)
        return false;

    const QString line = block.text();
    const int column = position - block.position();
    for (const MarkdownHighlighter::VerbatimSpan &span : MarkdownHighlighter::verbatimSpans(line)) {
        if (column > span.span.start && column <= span.span.start + span.span.length)
            return false;
    }
    const int open = int(line.lastIndexOf(QStringLiteral("[["), qMax(0, column - 1)));
    if (open >= 0) {
        const int close = int(line.indexOf(QStringLiteral("]]"), open));
        if (close < 0 || close + 2 > column)
            return false;
    }

    int tokenStart = column;
    while (tokenStart > 0 && !line.at(tokenStart - 1).isSpace())
        --tokenStart;
    const QString token = line.mid(tokenStart, column - tokenStart);
    return !token.contains(QStringLiteral("://")) && !token.startsWith(QStringLiteral("www."))
        && !token.contains(QLatin1Char('@')) && !token.contains(QStringLiteral("]("));
}

// What to change, if anything, about the word just finished at position:
// the user's text replacements first, then the system's spelling correction.
QVariantMap Backend::autocorrection(int position) {
    if (!m_document || !SpellChecker::available() || !isProseAt(position))
        return {};
    const QString text = currentDocumentText();
    if (position <= 0 || position > text.length())
        return {};

    int tokenStart = position;
    while (tokenStart > 0 && !text.at(tokenStart - 1).isSpace())
        --tokenStart;
    const QString token = text.mid(tokenStart, position - tokenStart);
    const QString replaced = m_spellChecker->replacement(token);
    if (!replaced.isEmpty())
        return {{QStringLiteral("start"), tokenStart}, {QStringLiteral("end"), position},
                {QStringLiteral("replacement"), replaced}};

    if (!m_spellCheckEnabled || !m_autocorrectEnabled)
        return {};
    const QVariantMap word = wordAt(text, position);
    if (word.value(QStringLiteral("end")).toInt() != position)
        return {};
    const QString spelled = word.value(QStringLiteral("word")).toString();
    if (spelled.length() < 2 || !m_spellChecker->isMisspelled(spelled))
        return {};
    const QString corrected = m_spellChecker->correction(spelled);
    if (corrected.isEmpty() || corrected == spelled)
        return {};
    return {{QStringLiteral("start"), word.value(QStringLiteral("start"))},
            {QStringLiteral("end"), position},
            {QStringLiteral("replacement"), corrected}};
}

void Backend::lookUp(const QString &text, qreal x, qreal y, qreal pointSize) {
    SpellChecker::lookUp(m_parentWindow, text, QPointF(x, y), pointSize);
}

// Zoom moves in 10% steps between 50% and 300%; zero steps only snaps a
// stored value back onto that grid.
qreal Backend::steppedZoom(qreal zoom, int steps) {
    if (zoom <= 0)
        zoom = 1.0;
    return qBound(0.5, std::round((zoom + steps * 0.1) * 10) / 10, 3.0);
}

void Backend::zoomText(int steps) {
    const qreal zoom = steps == 0 ? 1.0 : steppedZoom(m_textZoom, steps);
    setStatus(QStringLiteral("Text size %1%").arg(qRound(zoom * 100)));
    if (qFuzzyCompare(zoom, m_textZoom))
        return;
    m_textZoom = zoom;
    QSettings().setValue(textZoomSetting, zoom);
    emit textZoomChanged();
}

void Backend::zoomInterface(int steps) {
    const qreal zoom = steps == 0 ? 1.0 : steppedZoom(m_interfaceZoom, steps);
    setStatus(QStringLiteral("Interface size %1%").arg(qRound(zoom * 100)));
    if (qFuzzyCompare(zoom, m_interfaceZoom))
        return;
    m_interfaceZoom = zoom;
    QSettings().setValue(interfaceZoomSetting, zoom);
    emit interfaceZoomChanged();
}

void Backend::attachDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    m_document = quickDocument->textDocument();
    m_lastDocumentText = m_document->toPlainText();
    m_highlighter = new MarkdownHighlighter(m_document);
    m_highlighter->setDarkMode(m_darkMode);
    m_highlighter->setPalette(m_palette);

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int, int charsAdded) {
                if (m_formattingTypography || m_loading)
                    return;
                m_lastChangePos = position;
                m_lastChangeAdded = charsAdded;
            });

    applyDocumentTypography();
    restoreRecovery();
}

void Backend::openDialog() {
    emit openDialogRequested();
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray contents = file.readAll();
    loadDocumentText(QString::fromUtf8(contents));
    clearRecovery();
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    setModified(false);
    setStatus(QStringLiteral("Opened %1").arg(fileName()));
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        saveAsDialog();
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::saveForClose() {
    if (!m_modified) {
        emit closeAfterSave();
        return;
    }

    m_closeAfterSave = true;
    save();
}

void Backend::saveAsDialog() {
    emit saveDialogRequested(suggestedSaveUrl());
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::fileDialogCanceled() {
    m_closeAfterSave = false;
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::reloadFromDisk() {
    if (m_fileUrl.isLocalFile())
        open(m_fileUrl);
}

void Backend::keepExternalVersion() {
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = file.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setModified(true);
    scheduleRecovery();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

void Backend::printDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        rendered.setDefaultFont(m_document->defaultFont());
        rendered.setMarkdown(currentDocumentText());
        rendered.print(&printer);
    }
}

namespace {
QString appleScriptString(const QString &text) {
    return QLatin1Char('"') + QString(text).replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
                                  .replace(QLatin1Char('"'), QStringLiteral("\\\"")) + QLatin1Char('"');
}

}

// AppleScript that shows the page in its own browser window: the window
// already showing it is reloaded and moved, otherwise a new one opens.
// With reloadOnly, a closed preview stays closed. Empty for browsers without
// the scripting support this needs.
QString Backend::browserWindowScript(const QString &browserId, const QString &url, const QRect &frame,
                            bool reloadOnly) {
    static const QStringList chromium = {
        QStringLiteral("com.google.Chrome"), QStringLiteral("com.google.Chrome.canary"),
        QStringLiteral("com.brave.Browser"), QStringLiteral("com.microsoft.edgemac"),
        QStringLiteral("com.vivaldi.Vivaldi"), QStringLiteral("org.chromium.Chromium")};
    static const QStringList safari = {QStringLiteral("com.apple.Safari"),
                                       QStringLiteral("com.apple.SafariTechnologyPreview")};
    const bool isChromium = chromium.contains(browserId);
    if (!isChromium && !safari.contains(browserId))
        return {};

    const QString tab = isChromium ? QStringLiteral("active tab") : QStringLiteral("current tab");
    const QString bounds = frame.isValid()
        ? QStringLiteral("set bounds of preview to {%1, %2, %3, %4}")
              .arg(frame.left()).arg(frame.top()).arg(frame.right() + 1).arg(frame.bottom() + 1)
        : QString();
    const QString opening = reloadOnly ? QStringLiteral("return")
        : isChromium ? QStringLiteral("set preview to make new window\n"
                                      "    set URL of active tab of preview to page")
                     : QStringLiteral("make new document with properties {URL:page}\n"
                                      "    set preview to front window");
    const QString reloading = isChromium ? QStringLiteral("tell active tab of preview to reload")
                                         : QStringLiteral("set URL of current tab of preview to page");
    return QStringLiteral(R"(set page to %1
tell application id %2
  set preview to missing value
  repeat with candidate in windows
    try
      if (URL of %3 of candidate) is page then set preview to candidate
    end try
  end repeat
  if preview is missing value then
    %4
  else
    %5
  end if
  %6
end tell)")
        .arg(appleScriptString(url), appleScriptString(browserId), tab, opening, reloading, bounds);
}

// Writes the note as a themed HTML page in the cache and returns its URL. Each
// note keeps one page there, so writing again just refreshes it.
QUrl Backend::writePreview() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/preview");
    QDir().mkpath(directory);
    const QString title = QFileInfo(fileName()).completeBaseName();
    const QString path = QDir(directory).filePath(
        QString(title).replace(QRegularExpression(QStringLiteral("[^\\w .-]")), QStringLiteral("-"))
        + QStringLiteral(".html"));

    const QUrl baseUrl = m_fileUrl.isLocalFile()
        ? QUrl::fromLocalFile(QFileInfo(m_fileUrl.toLocalFile()).absolutePath() + QLatin1Char('/'))
        : QUrl();
    const Preview::Style style{m_palette, m_darkMode, m_editorFont,
                               qRound(20 * m_textScale * m_textZoom)};

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not write the preview."));
        return {};
    }
    file.write(Preview::html(currentDocumentText(), title, baseUrl, style).toUtf8());
    if (!file.commit()) {
        setStatus(QStringLiteral("Could not write the preview."));
        return {};
    }
    return QUrl::fromLocalFile(path);
}

// Opens the rendered note in its own browser window, placed at frame (screen
// coordinates) when the browser allows it, and keeps Omawrite in front.
void Backend::previewInBrowser(const QRect &frame) {
    const QUrl page = writePreview();
    if (page.isEmpty())
        return;
    m_previewPage = page;
    m_previewFrame = frame;
    showBrowserPreview(false);
}

// After a save, the open preview window catches up; a closed one stays closed.
void Backend::refreshBrowserPreview() {
    if (m_previewPage.isEmpty() || !sameFile(m_previewPage, writePreview()))
        return;
    showBrowserPreview(true);
}

void Backend::showBrowserPreview(bool reloadOnly) {
    const QString url = m_previewPage.toString(QUrl::FullyEncoded);
#ifdef Q_OS_MACOS
    const QString browserId = MacIntegration::defaultBrowserId();
    const QString script = browserWindowScript(browserId, url, m_previewFrame, reloadOnly);
    if (!script.isEmpty()) {
        auto *osascript = new QProcess(this);
        connect(osascript, &QProcess::finished, this, [this, osascript, reloadOnly](int exitCode) {
            const QString error = QString::fromUtf8(osascript->readAllStandardError());
            osascript->deleteLater();
            if (exitCode != 0 && !reloadOnly) {
                // Most likely Automation permission was declined.
                QDesktopServices::openUrl(m_previewPage);
                setStatus(error.contains(QStringLiteral("-1743"))
                              ? QStringLiteral("Preview opened in a tab; allow Omawrite to control your "
                                               "browser in Privacy & Security > Automation for a window")
                              : QStringLiteral("Opened preview in browser"));
            }
            MacIntegration::activateApp();
        });
        osascript->start(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), script});
        if (!reloadOnly)
            setStatus(QStringLiteral("Opened preview window"));
        return;
    }
    if (browserId == QStringLiteral("org.mozilla.firefox")) {
        if (!reloadOnly)
            QProcess::startDetached(MacIntegration::defaultBrowserPath()
                                        + QStringLiteral("/Contents/MacOS/firefox"),
                                    {QStringLiteral("-new-window"), url});
        QTimer::singleShot(400, this, [] { MacIntegration::activateApp(); });
        return;
    }
#endif
    if (!reloadOnly) {
        QDesktopServices::openUrl(m_previewPage);
        setStatus(QStringLiteral("Opened preview in browser"));
    }
}

bool Backend::sameFile(const QUrl &a, const QUrl &b) {
    return a.isLocalFile() && b.isLocalFile()
        && QDir::cleanPath(a.toLocalFile()) == QDir::cleanPath(b.toLocalFile());
}

bool Backend::isAtOrUnder(const QUrl &url, const QUrl &folder) {
    if (!url.isLocalFile() || !folder.isLocalFile())
        return false;
    const QString path = QDir::cleanPath(url.toLocalFile());
    const QString root = QDir::cleanPath(folder.toLocalFile());
    return path == root || path.startsWith(root + QLatin1Char('/'));
}

QUrl Backend::remapped(const QUrl &url, const QUrl &from, const QUrl &to) {
    if (!isAtOrUnder(url, from))
        return url;
    return QUrl::fromLocalFile(QDir::cleanPath(to.toLocalFile())
                               + QDir::cleanPath(url.toLocalFile()).mid(QDir::cleanPath(from.toLocalFile()).length()));
}

// The sidebar renamed or moved the open note, or a folder holding it.
void Backend::fileMoved(const QUrl &from, const QUrl &to) {
    if (!isAtOrUnder(m_fileUrl, from))
        return;
    setFileUrl(remapped(m_fileUrl, from, to));
    scheduleRecovery();
}

// After the open note goes to the Trash, start over on an empty page.
void Backend::closeDocument() {
    loadDocumentText(QString());
    clearRecovery();
    m_lastKnownFileContents.clear();
    m_hasKnownFileContents = false;
    setFileUrl(QUrl());
    setModified(false);
    setStatus(QString());
}

void Backend::newWindow() {
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                 QStringList());
    if (!started)
        setStatus(QStringLiteral("Could not open a new window."));
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::editorTextChanged() {
    if (m_loading || m_formattingTypography)
        return false;

    const QString text = currentDocumentText();
    if (text == m_lastDocumentText)
        return false;
    m_lastDocumentText = text;

    if (m_document) {
        const int blockCount = m_document->blockCount();
        if (blockCount > m_formattedBlockCount)
            reapplyTypographyToChange();
        m_formattedBlockCount = blockCount;
    }

    scheduleWordCount();
    setModified(true);
    setStatus(QStringLiteral("Unsaved"));
    scheduleRecovery();
    return true;
}

QVariantList Backend::hiddenRangesAt(int position) const {
    QVariantList ranges;
    if (!m_document)
        return ranges;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return ranges;
    // Nothing is hidden inside code and math blocks, so the caret moves freely.
    if (MarkdownHighlighter::isVerbatimState(block.userState())
            || block.previous().userState() == MarkdownHighlighter::MathState)
        return ranges;

    const int lineStart = block.position();
    QList<QPair<int, int>> spans;
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(block.text());
    for (const MarkdownHighlighter::InlineMarkup &item : markup) {
        for (const MarkdownHighlighter::Span &marker : item.markers) {
            spans.append({lineStart + marker.start,
                          lineStart + marker.start + marker.length});
        }
    }
    std::sort(spans.begin(), spans.end());

    for (const auto &span : spans) {
        ranges.append(QVariantMap{{QStringLiteral("start"), span.first},
                                  {QStringLiteral("end"), span.second}});
    }
    return ranges;
}

void Backend::setSearchHighlight(const QString &query, int currentMatchStart) {
    if (m_highlighter)
        m_highlighter->setSearch(query, currentMatchStart);
}

void Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto"))
        QDesktopServices::openUrl(url);
}

QVariantMap Backend::windowGeometry() const {
    QSettings settings;
    return {{QStringLiteral("x"), settings.value(QStringLiteral("window/x"), -1)},
            {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), -1)},
            {QStringLiteral("width"), settings.value(QStringLiteral("window/width"), 1280)},
            {QStringLiteral("height"), settings.value(QStringLiteral("window/height"), 820)},
            {QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false)}};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

QString Backend::defaultEditorFont() {
    return QStringLiteral("IBM Plex Mono");
}

void Backend::setEditorFont(const QString &family) {
    if (family.isEmpty() || family == m_editorFont)
        return;

    m_editorFont = family;
    QSettings().setValue(editorFontSetting, family);
    emit editorFontChanged();
}

QStringList Backend::availableFonts() const {
    // Leave out icon, emoji, and other symbol fonts, along with anything that
    // cannot set the Latin text the editor is written in.
    QStringList families;
    for (const QString &family : QFontDatabase::families(QFontDatabase::Latin)) {
        if (QFontDatabase::isPrivateFamily(family)
                || QFontDatabase::writingSystems(family).contains(QFontDatabase::Symbol))
            continue;
        families.append(family);
    }
    return selectableFontFamilies(families);
}

QStringList Backend::selectableFontFamilies(const QStringList &families) {
    QStringList selectable;
    static const QRegularExpression foundrySuffix(QStringLiteral("\\s*\\[[^\\]]*\\]$"));
    for (QString family : families) {
        // The same family from two foundries is listed as "Name [Foundry]".
        family.remove(foundrySuffix);
        if (family.startsWith(QStringLiteral("Noto ")) && !keptNotoFamilies.contains(family))
            continue;
        if (genericFontAliases.contains(family, Qt::CaseInsensitive)
                || family.contains(symbolFontName))
            continue;
        if (family == defaultEditorFont() || selectable.contains(family, Qt::CaseInsensitive))
            continue;
        selectable.append(family);
    }
    std::sort(selectable.begin(), selectable.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    selectable.prepend(defaultEditorFont());
    return selectable;
}

void Backend::documentFontChanged() {
    if (m_highlighter)
        m_highlighter->refreshFont();
}

void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    m_document->setPlainText(text);
    m_lastDocumentText = text;
    m_loading = false;

    applyDocumentTypography();
    m_wordCountTimer.stop();
    setWordCount(countWords(text));
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_fileUrl = url;
    emit fileUrlChanged();
    watchCurrentFile();
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void Backend::saveTo(const QUrl &url) {
    if (!url.isLocalFile()) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Only local files can be saved."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QByteArray contents = currentDocumentText().toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    const bool shouldClose = m_closeAfterSave;
    m_closeAfterSave = false;
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(url.toLocalFile()).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    clearRecovery();
    emit saveSucceeded();

    if (shouldClose)
        emit closeAfterSave();
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

void Backend::writeRecovery() {
    if (!m_modified)
        return;
    const QString path = recoveryPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("text"), currentDocumentText()}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());
    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = diskFile.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setFileUrl(recoveredUrl);
    setModified(true);
    setStatus(QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    QFile::remove(recoveryPath());
}

void Backend::watchCurrentFile() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

void Backend::loadOmarchyTheme() {
    // Only the default theme takes Omarchy's colors; picking another theme is
    // an explicit choice that should not be overridden.
    QHash<QString, QString> omarchy;
    if (m_theme == Themes::defaultId()) {
        QFile file(QDir::homePath()
                   + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;

                const int equals = line.indexOf(QLatin1Char('='));
                if (equals < 0)
                    continue;

                QString value = line.mid(equals + 1).trimmed();
                if (value.size() >= 2
                        && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                            || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                    value = value.mid(1, value.size() - 2);
                omarchy.insert(line.left(equals).trimmed(), value);
            }
        }
    }

    // An explicit mode wins; otherwise a theme background decides by luminance.
    bool themeIsDark = m_darkMode;
    const QString mode = omarchy.value(QStringLiteral("mode"));
    const QColor background(omarchy.value(QStringLiteral("background")));
    if (mode == QStringLiteral("dark") || mode == QStringLiteral("light")) {
        themeIsDark = mode == QStringLiteral("dark");
    } else if (background.isValid()) {
        themeIsDark = 0.299 * background.redF() + 0.587 * background.greenF()
            + 0.114 * background.blueF() < 0.5;
    }
    if (themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    m_palette = Themes::palette(m_theme, m_darkMode);
    if (!omarchy.isEmpty()) {
        m_palette.background = omarchy.value(QStringLiteral("background"), m_palette.background);
        m_palette.foreground = omarchy.value(QStringLiteral("foreground"), m_palette.foreground);
        m_palette.accent = omarchy.value(QStringLiteral("accent"), m_palette.accent);
        m_palette.selection = omarchy.value(QStringLiteral("selection"), m_palette.selection);
        m_palette.heading = m_palette.foreground;
    }
    m_themeBackground = m_palette.background;
    m_themeForeground = m_palette.foreground;
    m_themeAccent = m_palette.accent;
    m_themeSelection = m_palette.selection;

    if (m_highlighter) {
        m_highlighter->setDarkMode(m_darkMode);
        m_highlighter->setPalette(m_palette);
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl;

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(
        directory.filePath(suggestedFileName(currentDocumentText())));
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    setWordCount(countWords(currentDocumentText()));
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // A full pass is only used for freshly loaded/attached documents, so it is
    // safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(blockFormat);
    m_formattingTypography = false;

    m_document->setUndoRedoEnabled(undoEnabled);

    m_formattedBlockCount = m_document->blockCount();
}

void Backend::reapplyTypographyToChange() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // Format only the block(s) touched by the last edit instead of the whole
    // document, and fold the change into the preceding edit command so a single
    // undo reverts both the text and its formatting.
    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.joinPreviousEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(blockFormat);
    cursor.endEditBlock();
    m_formattingTypography = false;
}
