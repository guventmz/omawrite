#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

#include "backend.h"
#include "markdownhighlighter.h"
#include "vault.h"
#include "keymap.h"
#include "themes.h"
#include "spellchecker.h"
#include "preview.h"
#include <QTextBlock>
#include <QTextDocument>
#include <QImage>

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
        Keymap keymap;
        engine.rootContext()->setContextProperty(QStringLiteral("keymap"), &keymap);
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

    // The app loads its interface from resources; a file missing from
    // resources.qrc would only show up there.
    void loadsTheInterfaceFromResources() {
        Backend backend;
        Vault vault;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
        Keymap keymap;
        engine.rootContext()->setContextProperty(QStringLiteral("keymap"), &keymap);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        Vault vault;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
        Keymap keymap;
        engine.rootContext()->setContextProperty(QStringLiteral("keymap"), &keymap);
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
            Keymap keymap;
            engine.rootContext()->setContextProperty(QStringLiteral("keymap"), &keymap);
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

    void checksSpellingInProseOnly() {
        if (!SpellChecker::available())
            QSKIP("No system spell checker on this platform");

        SpellChecker checker;
        const QList<SpellChecker::Range> wrong = checker.misspellings(QStringLiteral("I recieve the mail"));
        QCOMPARE(wrong.size(), 1);
        QCOMPARE(wrong.at(0).start, 2);
        QCOMPARE(wrong.at(0).length, 7);
        QVERIFY(checker.isMisspelled(QStringLiteral("recieve")));
        QVERIFY(!checker.isMisspelled(QStringLiteral("receive")));
        QVERIFY(checker.guesses(QStringLiteral("recieve")).contains(QStringLiteral("receive")));

        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { textFormat: TextEdit.PlainText }", QUrl());
        QScopedPointer<QObject> editor(component.create());
        QVERIFY(editor);
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        editor->setProperty("text", QStringLiteral(
            "Some prose with recieve\n```\nrecieve = 1\n```\nsee https://exmaple.com/recieve"));

        const QString text = editor->property("text").toString();
        const int prose = text.indexOf(QStringLiteral("recieve")) + 7;
        const QVariantMap fix = backend.autocorrection(prose);
        backend.setAutocorrectEnabled(true);
        const QVariantMap corrected = backend.autocorrection(prose);
        QCOMPARE(corrected.value(QStringLiteral("replacement")).toString(), QStringLiteral("receive"));
        QVERIFY(backend.autocorrection(text.indexOf(QStringLiteral("recieve = 1")) + 7).isEmpty());
        QVERIFY(backend.autocorrection(int(text.length())).isEmpty());
        Q_UNUSED(fix);

        QCOMPARE(backend.wordAt(QStringLiteral("it's here"), 2).value(QStringLiteral("word")).toString(),
                 QStringLiteral("it's"));
        backend.setAutocorrectEnabled(false);
    }

    void buildsABrowserPreview() {
        const QString note = QStringLiteral(
            "# Title\nInline $x_1^2$ and [[Deep Work|focus]].\n"
            "$$\nA = \\begin{bmatrix}1\\\\2\\end{bmatrix}\n$$\n"
            "```python\nprice = \"$5 and $6\"  # [[not a link]]\n```\n"
            "Ends </script> here.");
        Preview::Style style{Themes::palette(QStringLiteral("nord"), true), true,
                             QStringLiteral("IBM Plex Mono"), 22};
        const QString page = Preview::html(note, QStringLiteral("Title"),
                                           QUrl::fromLocalFile(QStringLiteral("/notes/")), style);

        QVERIFY(page.contains(QStringLiteral("OMAWRITEMATH0X")));
        QVERIFY(page.contains(QStringLiteral("OMAWRITEMATH1X")));
        QVERIFY(!page.contains(QStringLiteral("OMAWRITEMATH2X")));
        QVERIFY(page.contains(QStringLiteral("x_1^2")));
        QVERIFY(page.contains(QStringLiteral("<span class=\\\"wikilink\\\">focus<\\/span>")));
        QVERIFY(page.contains(QStringLiteral("# [[not a link]]")));
        QVERIFY(page.contains(QStringLiteral("<base href=\"file:///notes/\">")));
        QVERIFY(page.contains(QStringLiteral("#2e3440")));
        QVERIFY(page.contains(QStringLiteral("font-size: 22px")));
        QVERIFY(page.contains(QStringLiteral("@font-face")));
        QCOMPARE(page.count(QStringLiteral("</script>")), 5);
    }

    // Runs Vim.js over a plain string: keys are tokens; in insert mode a
    // single-character token is typed, as the real editor would.
    QVariantMap runVim(const QString &text, int cursor, const QStringList &keys) {
        static QQmlEngine engine;
        static QScopedPointer<QObject> driver;
        if (!driver) {
            QQmlComponent component(&engine);
            component.setData(R"(
import QtQuick
import "Vim.js" as Vim
QtObject {
    function run(text, cursor, keys) {
        var vim = Vim.create();
        var doc = { text: text, cursor: cursor, undo: [], redo: [], selection: [-1, -1], message: "" };
        var ed = {
            text: function() { return doc.text; },
            cursor: function() { return doc.cursor; },
            setCursor: function(p) { doc.cursor = p; },
            select: function(a, b) { doc.selection = [a, b]; },
            clearSelection: function() { doc.selection = [-1, -1]; },
            replace: function(a, b, s) {
                doc.undo.push([doc.text, doc.cursor]); doc.redo = [];
                doc.text = doc.text.slice(0, a) + s + doc.text.slice(b); doc.cursor = a + s.length;
            },
            undo: function() { if (doc.undo.length) { doc.redo.push([doc.text, doc.cursor]); var u = doc.undo.pop(); doc.text = u[0]; doc.cursor = u[1]; } },
            redo: function() { if (doc.redo.length) { doc.undo.push([doc.text, doc.cursor]); var r = doc.redo.pop(); doc.text = r[0]; doc.cursor = r[1]; } },
            pageLines: function() { return 10; },
            host: { save: function() { doc.message = "saved"; }, quit: function(f) { doc.message = f ? "quit!" : "quit"; },
                    openSearch: function() { doc.message = "search"; }, openCommand: function() { doc.message = "command"; },
                    message: function(m) { doc.message = m; }, modeChanged: function() {} }
        };
        for (var i = 0; i < keys.length; ++i) {
            if (!Vim.feed(vim, ed, keys[i]) && vim.mode === "insert") {
                var key = keys[i] === "<CR>" ? "\n" : keys[i];
                ed.replace(doc.cursor, doc.cursor, key);
            }
        }
        return { text: doc.text, cursor: doc.cursor, mode: vim.mode, register: vim.register.text,
                 selection: doc.selection, message: doc.message };
    }
})", QUrl::fromLocalFile(QFINDTESTDATA("../src/Vim.js")));
            driver.reset(component.create());
            if (!driver)
                qWarning() << component.errorString();
        }
        QVariant result;
        QMetaObject::invokeMethod(driver.data(), "run", Q_RETURN_ARG(QVariant, result),
                                  Q_ARG(QVariant, text), Q_ARG(QVariant, cursor), Q_ARG(QVariant, keys));
        return result.toMap();
    }

    void editsLikeVim_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("cursor");
        QTest::addColumn<QStringList>("keys");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("expectedCursor");
        QTest::addColumn<QString>("mode");

        const QString line = QStringLiteral("one two three");
        QTest::newRow("w moves by word") << line << 0 << QStringList{"w", "w"} << line << 8 << "normal";
        QTest::newRow("e and b") << line << 0 << QStringList{"e", "e", "b"} << line << 4 << "normal";
        QTest::newRow("$ and 0") << line << 4 << QStringList{"$"} << line << 12 << "normal";
        QTest::newRow("dw") << line << 0 << QStringList{"d", "w"} << QStringLiteral("two three") << 0 << "normal";
        QTest::newRow("d2w") << line << 0 << QStringList{"d", "2", "w"} << QStringLiteral("three") << 0 << "normal";
        QTest::newRow("dw keeps the newline") << QStringLiteral("one two\nnext") << 4 << QStringList{"d", "w"}
            << QStringLiteral("one \nnext") << 3 << "normal";
        QTest::newRow("cw then type") << line << 4 << QStringList{"c", "w", "2", "<Esc>"}
            << QStringLiteral("one 2 three") << 4 << "normal";
        QTest::newRow("ciw") << line << 5 << QStringList{"c", "i", "w", "x", "<Esc>"}
            << QStringLiteral("one x three") << 4 << "normal";
        QTest::newRow("daw") << line << 5 << QStringList{"d", "a", "w"} << QStringLiteral("one three") << 4 << "normal";
        QTest::newRow("ci\"") << QStringLiteral("say \"hello there\" now") << 7 << QStringList{"c", "i", "\"", "hi", "<Esc>"}
            << QStringLiteral("say \"hi\" now") << 6 << "normal";
        QTest::newRow("di(") << QStringLiteral("f(a, (b), c)") << 3 << QStringList{"d", "i", "("}
            << QStringLiteral("f()") << 2 << "normal";
        QTest::newRow("x and p") << QStringLiteral("abc") << 0 << QStringList{"x", "p"} << QStringLiteral("bac") << 1 << "normal";
        QTest::newRow("dd") << QStringLiteral("a\nb\nc") << 2 << QStringList{"d", "d"} << QStringLiteral("a\nc") << 2 << "normal";
        QTest::newRow("dd last line") << QStringLiteral("a\nb\nc") << 4 << QStringList{"d", "d"} << QStringLiteral("a\nb") << 2 << "normal";
        QTest::newRow("yy p") << QStringLiteral("a\nb") << 0 << QStringList{"y", "y", "p"} << QStringLiteral("a\na\nb") << 2 << "normal";
        QTest::newRow("dd P") << QStringLiteral("a\nb") << 2 << QStringList{"d", "d", "P"} << QStringLiteral("b\na") << 0 << "normal";
        QTest::newRow("o opens below") << QStringLiteral("  a\nb") << 0 << QStringList{"o", "x", "<Esc>"}
            << QStringLiteral("  a\n  x\nb") << 6 << "normal";
        QTest::newRow("A appends") << QStringLiteral("ab\ncd") << 0 << QStringList{"A", "!", "<Esc>"}
            << QStringLiteral("ab!\ncd") << 2 << "normal";
        QTest::newRow("dot repeats") << QStringLiteral("a b c d") << 0 << QStringList{"d", "w", ".", "."}
            << QStringLiteral("d") << 0 << "normal";
        QTest::newRow("dot repeats insert") << QStringLiteral("x\ny") << 0 << QStringList{"A", ";", "<Esc>", "j", "."}
            << QStringLiteral("x;\ny;") << 4 << "normal";
        QTest::newRow("u and C-r") << line << 0 << QStringList{"d", "w", "u"} << line << 0 << "normal";
        QTest::newRow("redo") << line << 0 << QStringList{"d", "w", "u", "<C-r>"} << QStringLiteral("two three") << 0 << "normal";
        QTest::newRow("visual delete") << line << 4 << QStringList{"v", "e", "d"} << QStringLiteral("one  three") << 4 << "normal";
        QTest::newRow("visual line yank put") << QStringLiteral("a\nb") << 0 << QStringList{"V", "y", "j", "p"}
            << QStringLiteral("a\nb\na") << 4 << "normal";
        QTest::newRow("f and ;") << QStringLiteral("a-b-c") << 0 << QStringList{"f", "-", ";"} << QStringLiteral("a-b-c") << 3 << "normal";
        QTest::newRow("dt") << QStringLiteral("abc;def") << 0 << QStringList{"d", "t", ";"} << QStringLiteral(";def") << 0 << "normal";
        QTest::newRow("J joins") << QStringLiteral("a\n   b") << 0 << QStringList{"J"} << QStringLiteral("a b") << 1 << "normal";
        QTest::newRow(">> indents") << QStringLiteral("- item") << 0 << QStringList{">", ">"} << QStringLiteral("  - item") << 2 << "normal";
        QTest::newRow("gg and G") << QStringLiteral("a\nb\nc") << 0 << QStringList{"G", "g", "g"} << QStringLiteral("a\nb\nc") << 0 << "normal";
        QTest::newRow("2G") << QStringLiteral("a\nb\nc") << 0 << QStringList{"2", "G"} << QStringLiteral("a\nb\nc") << 2 << "normal";
        QTest::newRow("r replaces") << QStringLiteral("cat") << 0 << QStringList{"r", "b"} << QStringLiteral("bat") << 0 << "normal";
        QTest::newRow("~ toggles case") << QStringLiteral("abc") << 0 << QStringList{"~", "~"} << QStringLiteral("ABc") << 2 << "normal";
        QTest::newRow("j keeps column") << QStringLiteral("abcd\nx\nabcd") << 3 << QStringList{"j", "j"}
            << QStringLiteral("abcd\nx\nabcd") << 10 << "normal";
        QTest::newRow("insert stays") << QStringLiteral("") << 0 << QStringList{"i", "h", "i"} << QStringLiteral("hi") << 2 << "insert";
        QTest::newRow("Escape steps back") << QStringLiteral("ab") << 0 << QStringList{"A", "<Esc>"} << QStringLiteral("ab") << 1 << "normal";
        QTest::newRow("C-d without a view") << QStringLiteral("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl") << 0
            << QStringList{"<C-d>"} << QStringLiteral("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl") << 10 << "normal";
        QTest::newRow("n searches") << QStringLiteral("foo bar foo") << 0 << QStringList{"*"} << QStringLiteral("foo bar foo") << 8 << "normal";
    }

    void editsLikeVim() {
        QFETCH(QString, text);
        QFETCH(int, cursor);
        QFETCH(QStringList, keys);
        QFETCH(QString, expected);
        QFETCH(int, expectedCursor);
        QFETCH(QString, mode);
        const QVariantMap result = runVim(text, cursor, keys);
        QCOMPARE(result.value(QStringLiteral("text")).toString(), expected);
        QCOMPARE(result.value(QStringLiteral("cursor")).toInt(), expectedCursor);
        QCOMPARE(result.value(QStringLiteral("mode")).toString(), mode);
    }

    void runsVimCommands() {
        QCOMPARE(runVim(QStringLiteral("a"), 0, {":"}).value(QStringLiteral("message")).toString(), QStringLiteral("command"));
        QCOMPARE(runVim(QStringLiteral("a"), 0, {"/"}).value(QStringLiteral("message")).toString(), QStringLiteral("search"));
    }

    void remapsShortcuts() {
        {
            Keymap keymap;
            QCOMPARE(keymap.sequences(QStringLiteral("bold")), QStringList{QStringLiteral("Ctrl+B")});
            QVERIFY(!keymap.isCustom(QStringLiteral("bold")));
            QCOMPARE(Keymap::sequenceFor(Qt::Key_J, int(Qt::ControlModifier | Qt::ShiftModifier)),
                     QStringLiteral("Ctrl+Shift+J"));
            QVERIFY(Keymap::sequenceFor(Qt::Key_Shift, int(Qt::ShiftModifier)).isEmpty());

            // Taking Ctrl+K from "Markdown link" leaves it unbound and says so.
            QSignalSpy changed(&keymap, &Keymap::changed);
            QCOMPARE(keymap.assign(QStringLiteral("bold"), QStringLiteral("Ctrl+K")), QStringLiteral("Markdown link"));
            QCOMPARE(changed.count(), 1);
            QCOMPARE(keymap.sequences(QStringLiteral("bold")), QStringList{QStringLiteral("Ctrl+K")});
            QVERIFY(keymap.sequences(QStringLiteral("markdownLink")).isEmpty());
            QCOMPARE(keymap.labels().value(QStringLiteral("markdownLink")).toString(), QStringLiteral("—"));
            QCOMPARE(keymap.bindings().value(QStringLiteral("bold")).toStringList(), QStringList{QStringLiteral("Ctrl+K")});
        }
        Keymap reopened;
        QCOMPARE(reopened.sequences(QStringLiteral("bold")), QStringList{QStringLiteral("Ctrl+K")});
        reopened.reset(QStringLiteral("bold"));
        reopened.reset(QStringLiteral("markdownLink"));
        QCOMPARE(Keymap().sequences(QStringLiteral("markdownLink")), QStringList{QStringLiteral("Ctrl+K")});

        QStringList ids;
        for (const Keymap::Action &action : Keymap::actions()) {
            QVERIFY2(!ids.contains(action.id), qPrintable(action.id));
            ids.append(action.id);
        }
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

    void managesTheVaultTree() {
        QCOMPARE(Vault::renameLinks(QStringLiteral("See [[Deep Work]], [[deep work|it]], [[Deep Work#Ch 2]], [[Deep Worker]]."),
                                    {QStringLiteral("Deep Work")}, QStringLiteral("Focus")),
                 QStringLiteral("See [[Focus]], [[Focus|it]], [[Focus#Ch 2]], [[Deep Worker]]."));

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QDir(folder.path()).mkpath(QStringLiteral("books"));
        QDir(folder.path()).mkpath(QStringLiteral(".obsidian"));
        const auto write = [&folder](const QString &name, const QString &text) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text.toUtf8());
        };
        write(QStringLiteral("books/Deep Work.md"), QStringLiteral("# Deep Work\n"));
        write(QStringLiteral("Index.md"), QStringLiteral("Read [[Deep Work|this]] and [[books/Deep Work]].\n"));
        write(QStringLiteral("cover.png"), QStringLiteral("not a note"));

        Vault vault;
        vault.setPath(folder.path());
        const auto names = [&vault] {
            QStringList rows;
            for (const QVariant &row : vault.tree()) {
                const QVariantMap entry = row.toMap();
                rows.append(QString(entry.value(QStringLiteral("depth")).toInt() * 2, QLatin1Char(' '))
                            + entry.value(QStringLiteral("name")).toString());
            }
            return rows;
        };
        QCOMPARE(names(), QStringList({QStringLiteral("books"), QStringLiteral("cover.png"),
                                       QStringLiteral("Index")}));
        vault.setExpanded(QStringLiteral("books"), true);
        QCOMPARE(names(), QStringList({QStringLiteral("books"), QStringLiteral("  Deep Work"),
                                       QStringLiteral("cover.png"), QStringLiteral("Index")}));

        const QVariantMap renamed = vault.rename(QStringLiteral("books/Deep Work.md"), QStringLiteral("Focus"));
        QCOMPARE(renamed.value(QStringLiteral("path")).toString(), QStringLiteral("books/Focus.md"));
        QCOMPARE(renamed.value(QStringLiteral("updatedNotes")).toInt(), 1);
        QFile index(folder.filePath(QStringLiteral("Index.md")));
        QVERIFY(index.open(QIODevice::ReadOnly));
        QCOMPARE(index.readAll(), QByteArray("Read [[Focus|this]] and [[Focus]].\n"));
        QVERIFY(vault.rename(QStringLiteral("Index.md"), QStringLiteral("books/../x")).contains(QStringLiteral("path")));
        QVERIFY(vault.rename(QStringLiteral("books"), QStringLiteral("  ")).contains(QStringLiteral("error")));

        const QString note = vault.createNoteIn(QStringLiteral("books"));
        QCOMPARE(note, QStringLiteral("books/Untitled.md"));
        QCOMPARE(vault.createNoteIn(QStringLiteral("books")), QStringLiteral("books/Untitled 2.md"));
        const QString created = vault.createFolder(QString());
        QCOMPARE(created, QStringLiteral("New Folder"));

        QCOMPARE(vault.move(QStringLiteral("books/Untitled.md"), created).value(QStringLiteral("path")).toString(),
                 QStringLiteral("New Folder/Untitled.md"));
        QVERIFY(vault.move(QStringLiteral("books"), QStringLiteral("books")).contains(QStringLiteral("error")));
        QVERIFY(vault.move(QStringLiteral("books/Untitled 2.md"), created).contains(QStringLiteral("path")));
        QVERIFY(vault.move(QStringLiteral("New Folder/Untitled 2.md"), QStringLiteral("books"))
                    .contains(QStringLiteral("path")));
        QVERIFY(vault.findFolders(QStringLiteral("nf")).constFirst().toMap()
                    .value(QStringLiteral("path")).toString() == created);
    }

    void handlesImages() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QDir root(folder.path());
        root.mkpath(QStringLiteral("assets"));
        root.mkpath(QStringLiteral("notes/deep"));
        QImage pixel(4, 4, QImage::Format_ARGB32);
        pixel.fill(Qt::red);
        QVERIFY(pixel.save(folder.filePath(QStringLiteral("assets/Pasted image 20240423.png"))));
        QVERIFY(pixel.save(folder.filePath(QStringLiteral("notes/deep/local.png"))));
        const auto write = [&folder](const QString &name, const QString &text) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text.toUtf8());
        };
        write(QStringLiteral("notes/deep/Note.md"),
              QStringLiteral("![[Pasted image 20240423.png|300]]\n![x](pasted%20image%2020240423.png)\n![y](local.png)\n"));

        Vault vault;
        vault.setPath(folder.path());
        const QUrl note = QUrl::fromLocalFile(folder.filePath(QStringLiteral("notes/deep/Note.md")));
        const QUrl asset = QUrl::fromLocalFile(folder.filePath(QStringLiteral("assets/Pasted image 20240423.png")));
        QCOMPARE(vault.resolveImage(QStringLiteral("Pasted image 20240423.png|300"), note), asset);
        QCOMPARE(vault.resolveImage(QStringLiteral("pasted%20image%2020240423.png"), note), asset);
        QCOMPARE(vault.resolveImage(QStringLiteral("local.png"), note),
                 QUrl::fromLocalFile(folder.filePath(QStringLiteral("notes/deep/local.png"))));
        QVERIFY(vault.resolveImage(QStringLiteral("missing.png"), note).isEmpty());

        const QString text = QStringLiteral("before ![[a b.png|200]] after");
        QCOMPARE(vault.imageAt(text, 10), QStringLiteral("a b.png|200"));
        QVERIFY(vault.imageAt(text, 2).isEmpty());
        QCOMPARE(vault.imageOnLine(text, 2).value(QStringLiteral("target")).toString(), QStringLiteral("a b.png|200"));

        QCOMPARE(Vault::renameImageLinks(
                     QStringLiteral("![[Old.png]] ![[old.png|300]] ![a](Old.png) ![b](assets/Old.png) ![c](My%20Old.png) [[Old]]"),
                     QStringLiteral("Old.png"), QStringLiteral("New.png")),
                 QStringLiteral("![[New.png]] ![[New.png|300]] ![a](New.png) ![b](assets/New.png) ![c](My%20Old.png) [[Old]]"));

        const QVariantMap renamed = vault.rename(QStringLiteral("assets/Pasted image 20240423.png"), QStringLiteral("diagram"));
        QCOMPARE(renamed.value(QStringLiteral("path")).toString(), QStringLiteral("assets/diagram.png"));
        QCOMPARE(renamed.value(QStringLiteral("updatedNotes")).toInt(), 1);
        QFile updated(note.toLocalFile());
        QVERIFY(updated.open(QIODevice::ReadOnly));
        QCOMPARE(updated.readAll(), QByteArray("![[diagram.png|300]]\n![x](diagram.png)\n![y](local.png)\n"));

        QTemporaryDir outside;
        QVERIFY(pixel.save(outside.filePath(QStringLiteral("shot.png"))));
        QCOMPARE(vault.importImage(QUrl::fromLocalFile(outside.filePath(QStringLiteral("shot.png")))), QStringLiteral("shot.png"));
        QVERIFY(QFileInfo::exists(folder.filePath(QStringLiteral("assets/shot.png"))));
        QCOMPARE(vault.importImage(QUrl::fromLocalFile(outside.filePath(QStringLiteral("shot.png")))), QStringLiteral("shot 2.png"));
        QCOMPARE(vault.importImage(QUrl::fromLocalFile(folder.filePath(QStringLiteral("notes/deep/local.png")))),
                 QStringLiteral("local.png"));

        const auto spans = MarkdownHighlighter::verbatimSpans(QStringLiteral("see ![[x.png]] and ![a_b](y_z.png) `![[c]]`"));
        QCOMPARE(spans.size(), 3);
        QVERIFY(spans.at(0).image);
        QVERIFY(spans.at(1).image);
        QVERIFY(!spans.at(2).image);
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("![a_b](y_z.png) ![[x]]")).isEmpty());

        Preview::Style style{Themes::palette(Themes::defaultId(), true), true, QStringLiteral("IBM Plex Mono"), 20};
        const QString page = Preview::html(QStringLiteral("![[diagram.png|300]] ![[gone.png]]"), QStringLiteral("t"), QUrl(), style,
                                           [&vault, note](const QString &target) {
                                               return vault.resolveImage(target, note).toString(QUrl::FullyEncoded);
                                           });
        QVERIFY(page.contains(QStringLiteral("![diagram.png](<file://")));
        QVERIFY(page.contains(QStringLiteral("missing\\\">gone.png")));
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

    void keepsADailyJournal() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QDir(folder.path()).mkpath(QStringLiteral("old"));
        QFile older(folder.filePath(QStringLiteral("old/2026-01-02.md")));
        QVERIFY(older.open(QIODevice::WriteOnly));
        older.close();

        Vault vault;
        vault.setPath(folder.path());
        QCOMPARE(vault.journalEntry(QDate(2026, 1, 2)), QUrl::fromLocalFile(older.fileName()));

        const QDate day(2026, 10, 11);
        const QUrl entry = vault.journalEntry(day);
        QCOMPARE(entry, QUrl::fromLocalFile(folder.filePath(QStringLiteral("Journal/2026-10-11.md"))));
        QFile entryFile(entry.toLocalFile());
        QVERIFY(entryFile.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(entryFile.readAll()),
                 QStringLiteral("# %1\n\n").arg(QLocale().toString(day, QLocale::LongFormat)));
        QCOMPARE(vault.journalEntry(day), entry);
        QCOMPARE(vault.resolveLink(QStringLiteral("2026-10-11")), entry);

        const QUrl later = vault.journalEntry(QDate(2026, 10, 12));
        QCOMPARE(vault.adjacentJournalEntry(entry, -1), QUrl::fromLocalFile(older.fileName()));
        QCOMPARE(vault.adjacentJournalEntry(entry, 1), later);
        QVERIFY(vault.adjacentJournalEntry(later, 1).isEmpty());
        QVERIFY(!Vault::journalDate(QStringLiteral("2026-13-01")).isValid());
        QVERIFY(!Vault::journalDate(QStringLiteral("Notes 2026-10-11")).isValid());
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmawriteTest)
#include "tst_omawrite.moc"
