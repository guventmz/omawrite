#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Every app shortcut by action, with per-platform defaults and the user's
// changes on top. Shortcuts in QML bind to bindings[action]; the shortcut
// sheet edits them.
class Keymap : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap bindings READ bindings NOTIFY changed)
    Q_PROPERTY(QVariantMap labels READ labels NOTIFY changed)
    Q_PROPERTY(QVariantList actions READ actionList CONSTANT)
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)

public:
    struct Action {
        QString id;
        QString title;
        QString group;
        QStringList defaults;
    };

    explicit Keymap(QObject *parent = nullptr);

    static const QList<Action> &actions();
    QVariantList actionList() const;
    QVariantMap bindings() const;
    QVariantMap labels() const;
    bool recording() const { return m_recording; }
    void setRecording(bool recording);

    QStringList sequences(const QString &id) const;
    Q_INVOKABLE bool isCustom(const QString &id) const;
    Q_INVOKABLE QString assign(const QString &id, const QString &sequence);
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE static QString sequenceFor(int key, int modifiers);
    static QString label(const QStringList &sequences);

signals:
    void changed();
    void recordingChanged();

private:
    void store(const QString &id, const QStringList &sequences);

    QHash<QString, QStringList> m_custom;
    bool m_recording = false;
};
