#pragma once

#include <QColor>
#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QString>

// Serves image://math/<pixels>/<device pixel ratio>/<#aarrggbb>/<latex as
// UTF-16 hex>, rendering LaTeX math with MicroTeX off the GUI thread.
class MathImageProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    static QImage render(const QString &latex, qreal pixelSize, const QColor &color,
                         qreal devicePixelRatio, QString *error);
};
