#pragma once

#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

#include "themes.h"

class MarkdownHighlighter;
class QTextDocument;
class QWindow;
class QLockFile;
class SpellChecker;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileUrlChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY wordCountChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(qreal textZoom READ textZoom NOTIFY textZoomChanged)
    Q_PROPERTY(qreal interfaceZoom READ interfaceZoom NOTIFY interfaceZoomChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themeColorsChanged)
    Q_PROPERTY(QString wrapMode READ wrapMode WRITE setWrapMode NOTIFY wrapModeChanged)
    Q_PROPERTY(bool spellCheckAvailable READ spellCheckAvailable CONSTANT)
    Q_PROPERTY(bool spellCheckEnabled READ spellCheckEnabled WRITE setSpellCheckEnabled NOTIFY spellCheckChanged)
    Q_PROPERTY(bool autocorrectEnabled READ autocorrectEnabled WRITE setAutocorrectEnabled NOTIFY spellCheckChanged)
    Q_PROPERTY(bool vimMode READ vimMode WRITE setVimMode NOTIFY vimModeChanged)
    Q_PROPERTY(QString editorFont READ editorFont WRITE setEditorFont NOTIFY editorFontChanged)
    Q_PROPERTY(QStringList availableFonts READ availableFonts CONSTANT)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    void setParentWindow(QWindow *window);

    QUrl fileUrl() const { return m_fileUrl; }
    QString fileName() const;

    bool modified() const { return m_modified; }
    QString status() const { return m_status; }
    int wordCount() const { return m_wordCount; }
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    qreal textZoom() const { return m_textZoom; }
    qreal interfaceZoom() const { return m_interfaceZoom; }
    static qreal steppedZoom(qreal zoom, int steps);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }
    QString theme() const { return m_theme; }
    void setTheme(const QString &theme);
    QVariantList themes() const;
    QString wrapMode() const { return m_wrapMode; }
    void setWrapMode(const QString &mode);
    Q_INVOKABLE void cycleWrapMode();
    bool spellCheckAvailable() const;
    bool spellCheckEnabled() const { return m_spellCheckEnabled; }
    void setSpellCheckEnabled(bool enabled);
    bool autocorrectEnabled() const { return m_autocorrectEnabled; }
    void setAutocorrectEnabled(bool enabled);
    bool vimMode() const { return m_vimMode; }
    void setVimMode(bool enabled);
    Q_INVOKABLE QVariantMap wordAt(const QString &text, int position) const;
    Q_INVOKABLE bool isMisspelled(const QString &word);
    Q_INVOKABLE QStringList spellingGuesses(const QString &word);
    Q_INVOKABLE void learnWord(const QString &word);
    Q_INVOKABLE void ignoreWord(const QString &word);
    Q_INVOKABLE QVariantMap autocorrection(int position);
    Q_INVOKABLE void setCaretPosition(int position) { m_caretPosition = position; }
    Q_INVOKABLE QVariantList misspellingsBetween(int start, int end);
    Q_INVOKABLE void lookUp(const QString &text, qreal x, qreal y, qreal pointSize);
    QString editorFont() const { return m_editorFont; }
    void setEditorFont(const QString &family);
    QStringList availableFonts() const;
    static QString defaultEditorFont();
    static QStringList selectableFontFamilies(const QStringList &families);
    static int countWords(const QString &text);
    static QString normalizedLinkUrl(const QString &clipboardText);
    static QString suggestedFileName(const QString &text);

    Q_INVOKABLE void attachDocument(QObject *textDocument);
    Q_INVOKABLE void openDialog();
    Q_INVOKABLE void open(const QUrl &url);
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveForClose();
    Q_INVOKABLE void saveAsDialog();
    Q_INVOKABLE void saveAs(const QUrl &url);
    Q_INVOKABLE void fileDialogCanceled();
    Q_INVOKABLE void discardRecovery();
    Q_INVOKABLE void reloadFromDisk();
    Q_INVOKABLE void keepExternalVersion();
    Q_INVOKABLE void printDocument();
    Q_INVOKABLE void previewInBrowser();
    QUrl writePreview();
    Q_INVOKABLE void fileMoved(const QUrl &from, const QUrl &to);
    Q_INVOKABLE void closeDocument();
    Q_INVOKABLE void showStatus(const QString &status) { setStatus(status); }
    // URLs reach QML both percent-encoded and not, so files are compared by path.
    Q_INVOKABLE static bool sameFile(const QUrl &a, const QUrl &b);
    Q_INVOKABLE static bool isAtOrUnder(const QUrl &url, const QUrl &folder);
    Q_INVOKABLE static QUrl remapped(const QUrl &url, const QUrl &from, const QUrl &to);
    Q_INVOKABLE void newWindow();
    Q_INVOKABLE QString clipboardUrl() const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE bool editorTextChanged();
    Q_INVOKABLE QVariantList hiddenRangesAt(int position) const;
    Q_INVOKABLE void setSearchHighlight(const QString &query, int currentMatchStart);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);
    Q_INVOKABLE void documentFontChanged();
    Q_INVOKABLE void zoomText(int steps);
    Q_INVOKABLE void zoomInterface(int steps);

signals:
    void fileUrlChanged();
    void modifiedChanged();
    void statusChanged();
    void wordCountChanged();
    void darkModeChanged();
    void textScaleChanged();
    void textZoomChanged();
    void themeChanged();
    void wrapModeChanged();
    void spellCheckChanged();
    void spellingChanged();
    void vimModeChanged();
    void interfaceZoomChanged();
    void themeColorsChanged();
    void editorFontChanged();
    void closeAfterSave();
    void openDialogRequested();
    void saveDialogRequested(const QUrl &suggestedUrl);
    void saveSucceeded();
    void externalChangeDetected(bool deleted, bool locallyModified);

private:
    void loadDocumentText(const QString &text);
    void setFileUrl(const QUrl &url);
    void setModified(bool modified);
    void setStatus(const QString &status);
    void saveTo(const QUrl &url);
    QUrl suggestedSaveUrl() const;
    QString currentDocumentText() const;
    void setWordCount(int words);
    void refreshWordCount();
    void scheduleWordCount();
    void applyDocumentTypography();
    void reapplyTypographyToChange();
    void scheduleRecovery();
    void writeRecovery();
    void restoreRecovery();
    void clearRecovery();
    QString recoveryPath() const;
    void watchCurrentFile();
    void loadOmarchyTheme();
    void watchOmarchyTheme();

    QUrl m_fileUrl;
    bool m_modified = false;
    QString m_status;
    int m_wordCount = 0;
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    qreal m_textZoom = 1.0;
    qreal m_interfaceZoom = 1.0;
    bool m_loading = false;
    bool m_closeAfterSave = false;
    bool m_formattingTypography = false;
    int m_formattedBlockCount = 0;
    int m_lastChangePos = 0;
    int m_lastChangeAdded = 0;
    QTimer m_wordCountTimer;
    QTimer m_recoveryTimer;
    QFileSystemWatcher m_fileWatcher;
    QPointer<QTextDocument> m_document;
    QPointer<QWindow> m_parentWindow;
    QPointer<MarkdownHighlighter> m_highlighter;
    QString m_lastDocumentText;
    QByteArray m_lastKnownFileContents;
    bool m_hasKnownFileContents = false;
    QString m_recoveryPath;
    std::unique_ptr<QLockFile> m_recoveryLock;

    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QString m_theme;
    QString m_wrapMode;
    ThemePalette m_palette;
    std::unique_ptr<SpellChecker> m_spellChecker;
    bool m_spellCheckEnabled = true;
    bool m_autocorrectEnabled = false;
    int m_caretPosition = -1;
    bool m_vimMode = false;
    bool isProseAt(int position) const;
    QString m_editorFont;
    QFileSystemWatcher m_themeWatcher;
};
