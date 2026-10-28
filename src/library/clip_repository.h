#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <sqlite3.h>

struct ClipRecord {
    qint64 id = 0;
    QString path;
    QString title;
    qint64 sizeBytes = 0;
    QDateTime importedAt;
    bool favorite = false;
    bool available = false;
};

class ClipRepository final {
public:
    explicit ClipRepository(const QString &databasePath);
    ~ClipRepository();

    ClipRepository(const ClipRepository &) = delete;
    ClipRepository &operator=(const ClipRepository &) = delete;

    bool isOpen() const;
    QString errorString() const;
    QList<ClipRecord> list(const QString &search = {}) const;
    qint64 importFile(const QString &path, QString *error = nullptr);
    bool removeFromLibrary(qint64 id, QString *error = nullptr);
    bool setFavorite(qint64 id, bool favorite, QString *error = nullptr);

private:
    bool initialize();
    bool execute(const char *sql, QString *error = nullptr) const;
    void setError(QString *target, const QString &message) const;

    sqlite3 *m_database = nullptr;
    mutable QString m_error;
};
