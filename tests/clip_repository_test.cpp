#include "../src/library/clip_repository.h"
#include "../src/library/clip_library_model.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class ClipRepositoryTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsAndSearchesImportedClips();
    void duplicatePathReturnsExistingEntry();
    void removingEntryKeepsMediaFile();
    void rejectsMissingAndUnsupportedFiles();
    void rejectsNewerDatabaseSchema();
    void modelImportSearchFavoriteRemoveWorkflow();
};

static QString makeFile(const QString &directory, const QString &name)
{
    const QString path = directory + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write("synthetic media fixture\n");
    file.close();
    return path;
}

void ClipRepositoryTest::persistsAndSearchesImportedClips()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString mediaPath = makeFile(temporary.path(), QStringLiteral("night-run.mp4"));
    QVERIFY(!mediaPath.isEmpty());
    const QString databasePath = temporary.path() + QStringLiteral("/catalog.sqlite3");

    qint64 id = 0;
    {
        ClipRepository repository(databasePath);
        QVERIFY2(repository.isOpen(), qPrintable(repository.errorString()));
        id = repository.importFile(mediaPath);
        QVERIFY(id > 0);
        QCOMPARE(repository.list().size(), 1);
        QCOMPARE(repository.list(QStringLiteral("night")).size(), 1);
        QCOMPARE(repository.list(QStringLiteral("missing")).size(), 0);
        QVERIFY(repository.setFavorite(id, true));
        QCOMPARE(repository.list().first().favorite, true);
    }

    ClipRepository reopened(databasePath);
    QVERIFY2(reopened.isOpen(), qPrintable(reopened.errorString()));
    const auto rows = reopened.list();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().id, id);
    QCOMPARE(rows.first().title, QStringLiteral("night-run"));
    QCOMPARE(rows.first().favorite, true);
    QVERIFY(rows.first().available);
}

void ClipRepositoryTest::duplicatePathReturnsExistingEntry()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString mediaPath = makeFile(temporary.path(), QStringLiteral("same-clip.webm"));
    ClipRepository repository(temporary.path() + QStringLiteral("/catalog.sqlite3"));
    QVERIFY(repository.isOpen());

    const qint64 first = repository.importFile(mediaPath);
    const qint64 second = repository.importFile(temporary.path() + QStringLiteral("/./same-clip.webm"));
    QVERIFY(first > 0);
    QCOMPARE(second, first);
    QCOMPARE(repository.list().size(), 1);
}

void ClipRepositoryTest::removingEntryKeepsMediaFile()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString mediaPath = makeFile(temporary.path(), QStringLiteral("kept.mkv"));
    ClipRepository repository(temporary.path() + QStringLiteral("/catalog.sqlite3"));
    QVERIFY(repository.isOpen());

    const qint64 id = repository.importFile(mediaPath);
    QVERIFY(id > 0);
    QVERIFY(repository.removeFromLibrary(id));
    QCOMPARE(repository.list().size(), 0);
    QVERIFY(QFileInfo::exists(mediaPath));
}

void ClipRepositoryTest::rejectsMissingAndUnsupportedFiles()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ClipRepository repository(temporary.path() + QStringLiteral("/catalog.sqlite3"));
    QVERIFY(repository.isOpen());

    QString error;
    QCOMPARE(repository.importFile(temporary.path() + QStringLiteral("/missing.mp4"), &error), 0);
    QVERIFY(!error.isEmpty());

    const QString documentPath = makeFile(temporary.path(), QStringLiteral("notes.txt"));
    QVERIFY(!documentPath.isEmpty());
    error.clear();
    QCOMPARE(repository.importFile(documentPath, &error), 0);
    QVERIFY(!error.isEmpty());
}

void ClipRepositoryTest::rejectsNewerDatabaseSchema()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString databasePath = temporary.path() + QStringLiteral("/catalog.sqlite3");
    sqlite3 *database = nullptr;
    const QByteArray encodedPath = databasePath.toUtf8();
    QCOMPARE(sqlite3_open(encodedPath.constData(), &database), SQLITE_OK);
    QCOMPARE(sqlite3_exec(database, "PRAGMA user_version = 9;", nullptr, nullptr, nullptr), SQLITE_OK);
    QCOMPARE(sqlite3_close(database), SQLITE_OK);

    ClipRepository repository(databasePath);
    QVERIFY(!repository.isOpen());
    QVERIFY(repository.errorString().contains(QStringLiteral("newer application version")));
}

void ClipRepositoryTest::modelImportSearchFavoriteRemoveWorkflow()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString mediaPath = makeFile(temporary.path(), QStringLiteral("model-flow.mp4"));
    QVERIFY(!mediaPath.isEmpty());

    ClipRepository repository(temporary.path() + QStringLiteral("/catalog.sqlite3"));
    QVERIFY(repository.isOpen());
    ClipLibraryModel model(repository);
    QCOMPARE(model.rowCount(), 0);

    model.importFile(mediaPath);
    QCOMPARE(model.rowCount(), 1);
    const qint64 clipId = model.data(model.index(0, 0), ClipLibraryModel::ClipIdRole).toLongLong();
    QCOMPARE(model.data(model.index(0, 0), ClipLibraryModel::TitleRole).toString(), QStringLiteral("model-flow"));

    model.setSearchQuery(QStringLiteral("model"));
    QCOMPARE(model.rowCount(), 1);
    model.setSearchQuery(QStringLiteral("no-match"));
    QCOMPARE(model.rowCount(), 0);
    model.setSearchQuery(QString());
    QCOMPARE(model.rowCount(), 1);

    model.toggleFavorite(clipId);
    QCOMPARE(model.data(model.index(0, 0), ClipLibraryModel::FavoriteRole).toBool(), true);
    model.removeFromLibrary(clipId);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(QFileInfo::exists(mediaPath));
}

QTEST_GUILESS_MAIN(ClipRepositoryTest)
#include "clip_repository_test.moc"
