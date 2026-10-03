#include "mathrenderer.h"

#include <QCoreApplication>
#include <QMutex>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>

#include <memory>
#include <mutex>

#include "latex.h"
#include "platform/qt/graphic_qt.h"

namespace {
constexpr int maxLatexLength = 4000;
constexpr int renderTimeoutMs = 4000;

// MicroTeX keeps parser state in statics, so every render goes through one
// thread. The pool is deliberately never destroyed: if a pathological formula
// ever wedges that thread, quitting must not wait for it.
QThreadPool *renderPool() {
    static QThreadPool *pool = [] {
        auto *created = new QThreadPool;
        created->setMaxThreadCount(1);
        created->setExpiryTimeout(-1);
        return created;
    }();
    return pool;
}

QString decodeLatex(const QString &hex) {
    QString latex;
    for (int i = 0; i + 4 <= hex.length(); i += 4)
        latex.append(QChar(char16_t(hex.mid(i, 4).toUShort(nullptr, 16))));
    return latex;
}

class MathResponse : public QQuickImageResponse {
public:
    MathResponse(const QString &latex, qreal pixels, const QColor &color, qreal ratio) {
        QPointer<MathResponse> self(this);
        renderPool()->start(QRunnable::create([self, latex, pixels, color, ratio] {
            QString error;
            const QImage image = MathImageProvider::render(latex, pixels, color, ratio, &error);
            QMetaObject::invokeMethod(qApp, [self, image, error] {
                if (self)
                    self->finish(image, error);
            });
        }));
        QTimer::singleShot(renderTimeoutMs, this, [this] {
            finish({}, QStringLiteral("This formula took too long to render"));
        });
    }

    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override { return m_error; }

private:
    void finish(const QImage &image, const QString &error) {
        if (m_done)
            return;
        m_done = true;
        m_image = image;
        m_error = error;
        emit finished();
    }

    QImage m_image;
    QString m_error;
    bool m_done = false;
};
}

QQuickImageResponse *MathImageProvider::requestImageResponse(const QString &id, const QSize &) {
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() != 4)
        return new MathResponse({}, 0, {}, 1);
    return new MathResponse(decodeLatex(parts.at(3)), parts.at(0).toDouble(),
                            QColor(parts.at(2)), parts.at(1).toDouble());
}

QImage MathImageProvider::render(const QString &latex, qreal pixelSize, const QColor &color,
                                 qreal devicePixelRatio, QString *error) {
    const QString source = latex.trimmed();
    if (source.isEmpty() || pixelSize <= 0) {
        *error = QStringLiteral("Nothing to show yet");
        return {};
    }
    // Self-referencing definitions make the parser loop forever.
    static const QRegularExpression definitionRe(
        QStringLiteral("\\\\(?:newcommand|renewcommand|def|let|newenvironment)\\b"));
    if (source.length() > maxLatexLength || source.contains(definitionRe)) {
        *error = QStringLiteral("Macro definitions are not previewed");
        return {};
    }

    static std::once_flag initialized;
    std::call_once(initialized, [] { tex::LaTeX::init(":/res"); });

    static QMutex parserMutex;
    QMutexLocker lock(&parserMutex);
    try {
        const float size = float(pixelSize);
        std::unique_ptr<tex::TeXRender> render(tex::LaTeX::parse(
            source.toStdWString(), 0, size, size / 3.f, tex::color(color.rgba())));
        if (!render || render->getWidth() <= 0 || render->getHeight() <= 0) {
            *error = QStringLiteral("Nothing to show yet");
            return {};
        }

        constexpr int padding = 2;
        const qreal ratio = qMax<qreal>(1, devicePixelRatio);
        QImage image(QSize(render->getWidth() + 2 * padding,
                           render->getHeight() + 2 * padding) * ratio,
                     QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        tex::Graphics2D_qt graphics(&painter);
        render->draw(graphics, padding, padding);
        return image;
    } catch (const std::exception &exception) {
        *error = QString::fromUtf8(exception.what());
    }
    return {};
}
