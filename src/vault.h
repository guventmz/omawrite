#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

// A folder of plain Markdown notes. Notes link to each other with [[Title]]
// wikilinks, which resolve to Title.md anywhere under the folder.
class Vault : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(QString name READ name NOTIFY pathChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY pathChanged)

public:
    explicit Vault(QObject *parent = nullptr);

    QString path() const { return m_path; }
    QString name() const;
    bool ready() const;

    Q_INVOKABLE void setPath(const QString &path);
    Q_INVOKABLE void setFolder(const QUrl &folder);
    Q_INVOKABLE bool contains(const QUrl &fileUrl) const;
    Q_INVOKABLE void refresh();

    Q_INVOKABLE QVariantList findNotes(const QString &query, int limit = 50);
    Q_INVOKABLE QVariantList searchText(const QString &query, int limit = 200);
    Q_INVOKABLE QVariantList backlinks(const QUrl &noteUrl);
    Q_INVOKABLE int backlinkCount(const QUrl &noteUrl);
    Q_INVOKABLE QUrl resolveLink(const QString &target);
    Q_INVOKABLE QUrl createNote(const QString &title);
    Q_INVOKABLE QString linkAt(const QString &text, int position) const;

    static int fuzzyScore(const QString &query, const QString &candidate);
    static QString linkTarget(const QString &linkBody);
    static QStringList wikiLinkTargets(const QString &text);

signals:
    void pathChanged();

private:
    struct Note {
        QString path;
        QString title;
        QString relativeName;
        QDateTime modified;
    };

    struct CachedText {
        QDateTime modified;
        QString text;
    };

    void ensureScanned();
    QString noteText(const Note &note);
    QVariantMap noteEntry(const Note &note) const;

    QString m_path;
    QList<Note> m_notes;
    bool m_scanned = false;
    QHash<QString, CachedText> m_textCache;
};
