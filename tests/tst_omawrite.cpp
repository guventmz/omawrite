#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

#include "backend.h"
#include "markdownhighlighter.h"
#include "vault.h"
#include "themes.h"
#include <QTextBlock>
#include <QTextDocument>

class OmawriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void keepsNotoFromCrowdingTheFontList() {
        const QStringList fonts = Backend::selectableFontFamilies({
            QStringLiteral("Noto Sans Devanagari"), QStringLiteral("Liberation Serif"),
            QStringLiteral("Noto Serif"), QStringLiteral("Noto Sans Tamil UI"),
            QStringLiteral("adwaita Sans"), QStringLiteral("IBM Plex Mono"),
            QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans Mono"),
            QStringLiteral("Liberation Serif"), QStringLiteral("Monospace"),
            QStringLiteral("Standard Symbols PS"), QStringLiteral("D050000L"),
            QStringLiteral("Noto Color Emoji"), QStringLiteral("Nimbus Sans [UKWN]"),
            QStringLiteral("Nimbus Sans [URW ]")});
        QCOMPARE(fonts, QStringList({QStringLiteral("IBM Plex Mono"),
                                     QStringLiteral("adwaita Sans"),
                                     QStringLiteral("Liberation Serif"),
                                     QStringLiteral("Nimbus Sans"),
                                     QStringLiteral("Noto Sans"),
                                     QStringLiteral("Noto Sans Mono"),
                                     QStringLiteral("Noto Serif")}));
    }

    void remembersEditorFont() {
        const QString installed = Backend().availableFonts().value(1);
        QVERIFY(!installed.isEmpty());

        {
            Backend backend;
            QCOMPARE(backend.editorFont(), Backend::defaultEditorFont());
            QSignalSpy fontSpy(&backend, &Backend::editorFontChanged);
            backend.setEditorFont(installed);
            QCOMPARE(fontSpy.count(), 1);
        }

        QCOMPARE(Backend().editorFont(), installed);

        // A remembered font that has since been uninstalled falls back.
        QSettings().setValue(QStringLiteral("editor/font"), QStringLiteral("No Such Font"));
        QCOMPARE(Backend().editorFont(), Backend::defaultEditorFont());
        QSettings().remove(QStringLiteral("editor/font"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        Vault vault;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        Vault vault;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void zoomsTextAndInterfaceSeparately() {
        QCOMPARE(Backend::steppedZoom(1.0, 1), 1.1);
        QCOMPARE(Backend::steppedZoom(0.5, -1), 0.5);
        QCOMPARE(Backend::steppedZoom(3.0, 1), 3.0);
        QCOMPARE(Backend::steppedZoom(1.23, 0), 1.2);

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        {
            Backend backend;
            Vault vault;
            QQmlEngine engine;
            engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
            engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
            QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
            QScopedPointer<QObject> window(component.create());
            QVERIFY2(window, qPrintable(component.errorString()));

            backend.zoomText(2);
            QCOMPARE(window->property("editorFontPixelSize").toInt(), 24);
            QCOMPARE(window->property("textScale").toReal(), 1.0);

            backend.zoomInterface(-1);
            QCOMPARE(window->property("textScale").toReal(), 0.9);
            QCOMPARE(window->property("editorFontPixelSize").toInt(), 24);
            QCOMPARE(backend.status(), QStringLiteral("Interface size 90%"));
        }

        Backend reopened;
        QCOMPARE(reopened.textZoom(), 1.2);
        QCOMPARE(reopened.interfaceZoom(), 0.9);
        reopened.zoomText(0);
        reopened.zoomInterface(0);
        QCOMPARE(Backend().textZoom(), 1.0);
        QCOMPARE(Backend().interfaceZoom(), 1.0);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

    void ranksFuzzyNoteMatches() {
        QVERIFY(Vault::fuzzyScore(QStringLiteral("zk"), QStringLiteral("Zettelkasten")) >= 0);
        QCOMPARE(Vault::fuzzyScore(QStringLiteral("xyz"), QStringLiteral("Zettelkasten")), -1);
        QVERIFY(Vault::fuzzyScore(QStringLiteral("idea"), QStringLiteral("Idea")) >
                Vault::fuzzyScore(QStringLiteral("idea"), QStringLiteral("Ideas about ideas")));
        QVERIFY(Vault::fuzzyScore(QStringLiteral("deep work"), QStringLiteral("Deep Work notes")) >
                Vault::fuzzyScore(QStringLiteral("deep work"), QStringLiteral("dee pwork")));
        QVERIFY(Vault::fuzzyScore(QStringLiteral("pk"), QStringLiteral("personal knowledge")) >
                Vault::fuzzyScore(QStringLiteral("pk"), QStringLiteral("upkeep")));
    }

    void parsesWikiLinks() {
        QCOMPARE(Vault::wikiLinkTargets(QStringLiteral("See [[Idea]] and [[Deep Work|this]], [[Notes#Top]].")),
                 QStringList({QStringLiteral("Idea"), QStringLiteral("Deep Work"),
                              QStringLiteral("Notes")}));
        QVERIFY(Vault::wikiLinkTargets(QStringLiteral("[not] a [[\nlink]]")).isEmpty());

        Vault vault;
        const QString text = QStringLiteral("first line\nread [[Deep Work|this]] now");
        QCOMPARE(vault.linkAt(text, text.indexOf(QStringLiteral("this"))), QStringLiteral("Deep Work"));
        QCOMPARE(vault.linkAt(text, text.indexOf(QStringLiteral("now"))), QString());
        QCOMPARE(vault.linkAt(text, 2), QString());
    }

    void hidesWikiLinkMarkers() {
        const QString plain = QStringLiteral("a [[Idea]] b");
        const QList<MarkdownHighlighter::InlineMarkup> markup = MarkdownHighlighter::inlineMarkup(plain);
        QCOMPARE(markup.size(), 1);
        QCOMPARE(plain.mid(markup[0].content.start, markup[0].content.length), QStringLiteral("Idea"));
        QCOMPARE(markup[0].markers[0].start, 2);
        QCOMPARE(markup[0].markers[0].length, 2);
        QCOMPARE(markup[0].markers[1].start, 8);
        QCOMPARE(markup[0].markers[1].length, 2);

        const QString aliased = QStringLiteral("[[Deep Work|this]]");
        const MarkdownHighlighter::InlineMarkup alias = MarkdownHighlighter::inlineMarkup(aliased).constFirst();
        QCOMPARE(aliased.mid(alias.content.start, alias.content.length), QStringLiteral("this"));
        QCOMPARE(alias.markers[0].length, 12);
    }

    void keepsCodeAndMathVerbatim() {
        // Markdown never applies inside inline code or math.
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("`a*b*c` and $x_1 + x_2$")).isEmpty());
        QCOMPARE(MarkdownHighlighter::inlineMarkup(QStringLiteral("$a_b$ but *real*")).size(), 1);

        const auto spans = MarkdownHighlighter::verbatimSpans(
            QStringLiteral("cost $5 and $6, but $x^2$ and $$\\frac{a}{b}$$ and `$y$`"));
        QCOMPARE(spans.size(), 3);
        QVERIFY(spans.at(0).math);
        QVERIFY(spans.at(1).math);
        QVERIFY(!spans.at(2).math);

        QTextDocument document;
        document.setPlainText(QStringLiteral(
            "# Title\n```python\n# comment, not a heading\nx = *1*\n```\n"
            "$$\nx=\\begin{bmatrix}x_1\\\\x_2\\end{bmatrix}\n$$\nafter"));
        MarkdownHighlighter highlighter(&document);
        highlighter.rehighlight();
        QStringList states;
        for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
            states.append(MarkdownHighlighter::isVerbatimState(block.userState())
                              ? QStringLiteral("v") : QStringLiteral("-"));
        QCOMPARE(states.join(QString()), QStringLiteral("-vvv-vv--"));
        QCOMPARE(MarkdownHighlighter::commentStyleFor(QStringLiteral("Python")),
                 MarkdownHighlighter::CommentStyle::Hash);
        QCOMPARE(MarkdownHighlighter::commentStyleFor(QStringLiteral("rust")),
                 MarkdownHighlighter::CommentStyle::Slash);

    }

    void remembersThemeAndWrap() {
        {
            Backend backend;
            QCOMPARE(backend.theme(), Themes::defaultId());
            QCOMPARE(backend.wrapMode(), QStringLiteral("column"));
            backend.setTheme(QStringLiteral("solarized"));
            backend.setTheme(QStringLiteral("not-a-theme"));
            QCOMPARE(backend.theme(), QStringLiteral("solarized"));
            QCOMPARE(backend.themeBackground(),
                     Themes::palette(QStringLiteral("solarized"), backend.darkMode()).background);
            backend.cycleWrapMode();
            backend.cycleWrapMode();
            QCOMPARE(backend.wrapMode(), QStringLiteral("off"));
            QCOMPARE(backend.status(), QStringLiteral("Wrap: off"));
            QCOMPARE(backend.themes().size(), Themes::all().size());
        }
        Backend reopened;
        QCOMPARE(reopened.theme(), QStringLiteral("solarized"));
        QCOMPARE(reopened.wrapMode(), QStringLiteral("off"));
        reopened.setTheme(Themes::defaultId());
        reopened.setWrapMode(QStringLiteral("column"));
    }

    void navigatesAVault() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto write = [&folder](const QString &name, const QString &text) {
            QDir(folder.path()).mkpath(QFileInfo(folder.filePath(name)).path());
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text.toUtf8());
        };
        write(QStringLiteral("Zettelkasten.md"), QStringLiteral("# Zettelkasten\nLinks to [[Deep Work]].\n"));
        write(QStringLiteral("books/Deep Work.md"), QStringLiteral("# Deep Work\nFocus beats hours.\n"));
        write(QStringLiteral("Daily.md"), QStringLiteral("Read [[deep work|Newport]] again.\nFocus today.\n"));
        write(QStringLiteral(".obsidian/Hidden.md"), QStringLiteral("[[Deep Work]]\n"));

        Vault vault;
        vault.setPath(folder.path());
        QVERIFY(vault.ready());

        const QVariantList all = vault.findNotes(QString());
        QCOMPARE(all.size(), 3);
        QCOMPARE(vault.findNotes(QStringLiteral("dw")).constFirst().toMap().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Deep Work"));
        QCOMPARE(vault.findNotes(QStringLiteral("books")).size(), 1);

        const QUrl deepWork = vault.resolveLink(QStringLiteral("deep work"));
        QCOMPARE(deepWork, QUrl::fromLocalFile(folder.filePath(QStringLiteral("books/Deep Work.md"))));
        QCOMPARE(vault.resolveLink(QStringLiteral("books/Deep Work#Focus")), deepWork);
        QVERIFY(vault.resolveLink(QStringLiteral("Missing")).isEmpty());

        const QVariantList links = vault.backlinks(deepWork);
        QCOMPARE(links.size(), 2);
        QCOMPARE(vault.backlinkCount(deepWork), 2);

        const QVariantList hits = vault.searchText(QStringLiteral("focus"));
        QCOMPARE(hits.size(), 2);
        const QVariantList both = vault.searchText(QStringLiteral("today focus"));
        QCOMPARE(both.size(), 1);
        QCOMPARE(both.constFirst().toMap().value(QStringLiteral("line")).toInt(), 1);

        const QUrl created = vault.createNote(QStringLiteral("New: idea"));
        QCOMPARE(created, QUrl::fromLocalFile(folder.filePath(QStringLiteral("New- idea.md"))));
        QFile createdFile(created.toLocalFile());
        QVERIFY(createdFile.open(QIODevice::ReadOnly));
        QCOMPARE(createdFile.readAll(), QByteArray("# New- idea\n\n"));
        QCOMPARE(vault.createNote(QStringLiteral("deep work")), deepWork);
        QVERIFY(vault.contains(created));
        QVERIFY(!vault.contains(QUrl::fromLocalFile(QDir::homePath() + QStringLiteral("/x.md"))));
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmawriteTest)
#include "tst_omawrite.moc"
