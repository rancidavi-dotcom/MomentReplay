#include "clip_repository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <memory>

namespace {

struct StatementDeleter {
    void operator()(sqlite3_stmt *statement) const
    {
        if (statement)
            sqlite3_finalize(statement);
    }
};

using Statement = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

Statement prepare(sqlite3 *database, const char *sql, QString *error)
{
    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &raw, nullptr) != SQLITE_OK) {
        if (error)
            *error = QString::fromUtf8(sqlite3_errmsg(database));
        return {};
    }
    return Statement(raw);
}

bool bindText(sqlite3_stmt *statement, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    return sqlite3_bind_text(statement, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}

QString columnText(sqlite3_stmt *statement, int column)
{
    const auto *text = sqlite3_column_text(statement, column);
    return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

QString stablePath(const QFileInfo &info)
{
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

} // namespace

ClipRepository::ClipRepository(const QString &databasePath)
{
    const QFileInfo databaseInfo(databasePath);
    QDir().mkpath(databaseInfo.absolutePath());

    const QByteArray path = databaseInfo.absoluteFilePath().toUtf8();
    if (sqlite3_open_v2(path.constData(), &m_database,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        m_error = m_database ? QString::fromUtf8(sqlite3_errmsg(m_database))
                             : QStringLiteral("SQLite could not open the library database.");
        if (m_database) {
            sqlite3_close(m_database);
            m_database = nullptr;
        }
        return;
    }

    sqlite3_busy_timeout(m_database, 3000);
    if (!initialize()) {
        sqlite3_close(m_database);
        m_database = nullptr;
        return;
    }

    QFileInfo createdDatabase(databaseInfo.absoluteFilePath());
    if (createdDatabase.exists())
        QFile::setPermissions(createdDatabase.absoluteFilePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

ClipRepository::~ClipRepository()
{
    if (m_database)
        sqlite3_close(m_database);
}

bool ClipRepository::isOpen() const
{
    return m_database != nullptr;
}

QString ClipRepository::errorString() const
{
    return m_error;
}

QList<ClipRecord> ClipRepository::list(const QString &search) const
{
    QList<ClipRecord> records;
    if (!m_database)
        return records;

    QString error;
    auto statement = prepare(m_database,
        "SELECT id, path, title, size_bytes, imported_at, favorite "
        "FROM clips WHERE (? = '' OR title LIKE '%' || ? || '%' OR path LIKE '%' || ? || '%') "
        "ORDER BY imported_at DESC, id DESC",
        &error);
    if (!statement) {
        m_error = error;
        return records;
    }

    bindText(statement.get(), 1, search);
    bindText(statement.get(), 2, search);
    bindText(statement.get(), 3, search);

    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        ClipRecord record;
        record.id = sqlite3_column_int64(statement.get(), 0);
        record.path = columnText(statement.get(), 1);
        record.title = columnText(statement.get(), 2);
        record.sizeBytes = sqlite3_column_int64(statement.get(), 3);
        record.importedAt = QDateTime::fromString(columnText(statement.get(), 4), Qt::ISODateWithMs);
        record.favorite = sqlite3_column_int(statement.get(), 5) != 0;
        record.available = QFileInfo::exists(record.path);
        records.append(std::move(record));
    }

    if (result != SQLITE_DONE)
        m_error = QString::fromUtf8(sqlite3_errmsg(m_database));
    return records;
}

qint64 ClipRepository::importFile(const QString &path, QString *error)
{
    if (!m_database) {
        setError(error, m_error.isEmpty() ? QStringLiteral("Library database is unavailable.") : m_error);
        return 0;
    }

    const QFileInfo info(path);
    static const QSet<QString> supportedExtensions = {
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mkv"),
        QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ts"),
        QStringLiteral("mts"), QStringLiteral("ogv"), QStringLiteral("png"),
        QStringLiteral("jpg"), QStringLiteral("jpeg")
    };
    if (!info.exists() || !info.isFile() || !supportedExtensions.contains(info.suffix().toLower())) {
        setError(error, QStringLiteral("Choose an existing video or image file in a supported format."));
        return 0;
    }

    const QString pathKey = stablePath(info);
    QString localError;
    auto findExisting = prepare(m_database, "SELECT id FROM clips WHERE path = ?", &localError);
    if (!findExisting) {
        setError(error, localError);
        return 0;
    }
    bindText(findExisting.get(), 1, pathKey);
    const int existingResult = sqlite3_step(findExisting.get());
    if (existingResult == SQLITE_ROW)
        return sqlite3_column_int64(findExisting.get(), 0);
    if (existingResult != SQLITE_DONE) {
        setError(error, QString::fromUtf8(sqlite3_errmsg(m_database)));
        return 0;
    }

    auto insert = prepare(m_database,
        "INSERT INTO clips(path, title, size_bytes, imported_at) VALUES(?, ?, ?, ?)",
        &localError);
    if (!insert || !bindText(insert.get(), 1, pathKey)
        || !bindText(insert.get(), 2, info.completeBaseName())
        || sqlite3_bind_int64(insert.get(), 3, info.size()) != SQLITE_OK
        || !bindText(insert.get(), 4, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))) {
        setError(error, localError.isEmpty() ? QString::fromUtf8(sqlite3_errmsg(m_database)) : localError);
        return 0;
    }

    if (sqlite3_step(insert.get()) != SQLITE_DONE) {
        setError(error, QString::fromUtf8(sqlite3_errmsg(m_database)));
        return 0;
    }
    return sqlite3_last_insert_rowid(m_database);
}

bool ClipRepository::removeFromLibrary(qint64 id, QString *error)
{
    if (!m_database) {
        setError(error, m_error);
        return false;
    }
    auto statement = prepare(m_database, "DELETE FROM clips WHERE id = ?", error);
    if (!statement || sqlite3_bind_int64(statement.get(), 1, id) != SQLITE_OK
        || sqlite3_step(statement.get()) != SQLITE_DONE) {
        setError(error, QString::fromUtf8(sqlite3_errmsg(m_database)));
        return false;
    }
    return true;
}

bool ClipRepository::setFavorite(qint64 id, bool favorite, QString *error)
{
    if (!m_database) {
        setError(error, m_error);
        return false;
    }
    auto statement = prepare(m_database, "UPDATE clips SET favorite = ? WHERE id = ?", error);
    if (!statement || sqlite3_bind_int(statement.get(), 1, favorite ? 1 : 0) != SQLITE_OK
        || sqlite3_bind_int64(statement.get(), 2, id) != SQLITE_OK
        || sqlite3_step(statement.get()) != SQLITE_DONE) {
        setError(error, QString::fromUtf8(sqlite3_errmsg(m_database)));
        return false;
    }
    return true;
}

bool ClipRepository::initialize()
{
    if (!execute("PRAGMA journal_mode=WAL;"))
        return false;

    QString error;
    auto versionStatement = prepare(m_database, "PRAGMA user_version;", &error);
    if (!versionStatement || sqlite3_step(versionStatement.get()) != SQLITE_ROW) {
        m_error = error.isEmpty() ? QString::fromUtf8(sqlite3_errmsg(m_database)) : error;
        return false;
    }
    const int schemaVersion = sqlite3_column_int(versionStatement.get(), 0);
    if (schemaVersion > 1) {
        m_error = QStringLiteral("The library database was created by a newer application version.");
        return false;
    }

    if (!execute("CREATE TABLE IF NOT EXISTS clips ("
                 "id INTEGER PRIMARY KEY,"
                 "path TEXT NOT NULL UNIQUE,"
                 "title TEXT NOT NULL,"
                 "size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),"
                 "imported_at TEXT NOT NULL,"
                 "favorite INTEGER NOT NULL DEFAULT 0 CHECK(favorite IN (0, 1))"
                 ");"))
        return false;

    return schemaVersion == 1 || execute("PRAGMA user_version = 1;");
}

bool ClipRepository::execute(const char *sql, QString *error) const
{
    char *message = nullptr;
    const int result = sqlite3_exec(m_database, sql, nullptr, nullptr, &message);
    if (result == SQLITE_OK)
        return true;
    const QString reason = message ? QString::fromUtf8(message) : QString::fromUtf8(sqlite3_errmsg(m_database));
    sqlite3_free(message);
    m_error = reason;
    setError(error, reason);
    return false;
}

void ClipRepository::setError(QString *target, const QString &message) const
{
    m_error = message;
    if (target)
        *target = message;
}
