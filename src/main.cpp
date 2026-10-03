#include <QFont>
#include <QFontDatabase>
#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QWindow>
#include <QFile>
#include <QFileOpenEvent>
#include <QProcess>

#include "backend.h"
#include "systemtheme.h"
#include "vault.h"
#include "mathrenderer.h"

namespace {
// Finder hands documents to the app as FileOpen events rather than arguments,
// both at launch and while it is running. An untouched window takes the file;
// otherwise it opens in a new window, like Ctrl+N does.
class FileOpenFilter : public QObject {
public:
    FileOpenFilter(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::FileOpen)
            return QObject::eventFilter(watched, event);

        const QString path = static_cast<QFileOpenEvent *>(event)->file();
        if (m_backend->fileUrl().isEmpty() && !m_backend->modified())
            m_backend->open(QUrl::fromLocalFile(path));
        else
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {path});
        return true;
    }

private:
    Backend *m_backend;
};
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omawrite"));
    app.setDesktopFileName(QStringLiteral("omawrite"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omawrite")));

    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/IBMPlexMono-Regular.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/IBMPlexMono-Italic.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/IBMPlexMono-Bold.ttf"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/IBMPlexMono-BoldItalic.ttf"));
    app.setOrganizationName(QStringLiteral("Omacom"));
    app.setOrganizationDomain(QStringLiteral("omacom.io"));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    Backend backend(&app);
    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend,
                     &Backend::setDarkMode);

    // Carry the desktop's text scale into the default font, so the chrome that
    // inherits it (dialog titles, buttons) grows along with the writing area.
    const QFont interfaceFont(QStringLiteral("IBM Plex Mono"));
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, &backend, interfaceFont, basePointSize]() {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * backend.textScale() * backend.interfaceZoom());
        app.setFont(scaled);
    };

    backend.setTextScale(systemTheme.textScale());
    applyInterfaceFont();
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend,
                     [&backend, applyInterfaceFont](qreal textScale) {
        backend.setTextScale(textScale);
        applyInterfaceFont();
    });
    QObject::connect(&backend, &Backend::interfaceZoomChanged, &app, applyInterfaceFont);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("math"), new MathImageProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    Vault vault(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Omawrite interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    backend.setParentWindow(qobject_cast<QWindow *>(engine.rootObjects().constFirst()));

    const QStringList args = app.arguments();
    if (args.size() > 1 && !backend.modified())
        backend.open(QUrl::fromLocalFile(args.at(1)));

    app.installEventFilter(new FileOpenFilter(&backend, &app));

    return app.exec();
}
