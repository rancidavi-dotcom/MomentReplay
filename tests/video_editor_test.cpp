#include "editor/video_editor.h"
#include "library/clip_library_model.h"
#include "library/clip_repository.h"

#include <QCryptographicHash>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

bool runProcess(const QString &program, const QStringList &arguments, QString *output = nullptr)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(30000))
        return false;
    if (output)
        *output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace

class VideoEditorTest final : public QObject {
    Q_OBJECT

private slots:
    void projectRestoresAndExportCreatesTrimmedLibraryCopy()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        QVERIFY(!ffmpeg.isEmpty());
        QVERIFY(!ffprobe.isEmpty());

        const QString source = temporary.filePath(QStringLiteral("synthetic-source.mp4"));
        QVERIFY(runProcess(ffmpeg, {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("testsrc2=size=640x360:rate=30:duration=7"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("sine=frequency=660:duration=7"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-g"), QStringLiteral("90"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), source
        }));
        const QByteArray originalHash = fileHash(source);
        QVERIFY(!originalHash.isEmpty());

        ClipRepository repository(temporary.filePath(QStringLiteral("library.sqlite3")));
        QVERIFY(repository.isOpen());
        ClipLibraryModel library(repository);
        library.importFile(source);
        QCOMPARE(library.clipCount(), 1);

        const QString projects = temporary.filePath(QStringLiteral("projects"));
        const QString exports = temporary.filePath(QStringLiteral("exports"));
        VideoEditor editor(library, projects, exports);
        QCOMPARE(editor.exportPreset(), 1); // Balanced is the safe default.
        QCOMPARE(editor.exportPresetName(), QStringLiteral("Balanced"));
        editor.setExportPreset(0);
        QCOMPARE(editor.exportPresetName(), QStringLiteral("Compact"));
        editor.setExportPreset(2);
        QCOMPARE(editor.exportPresetName(), QStringLiteral("High quality"));
        editor.setExportPreset(3);
        QCOMPARE(editor.exportPresetName(), QStringLiteral("Maximum quality"));
        editor.setExportPreset(1);
        editor.openClip(source);
        editor.setDurationMs(7000);
        editor.setInMs(1350);
        editor.setOutMs(4350);
        QVERIFY(editor.dirty());
        QTRY_VERIFY_WITH_TIMEOUT(!editor.dirty(), 5000);
        QVERIFY(!editor.dirty());
        QVERIFY(QFile::exists(editor.projectPath()));

        VideoEditor restored(library, projects, exports);
        restored.openClip(source);
        QCOMPARE(restored.inMs(), qint64(1350));
        QCOMPARE(restored.outMs(), qint64(4350));
        QCOMPARE(restored.durationMs(), qint64(7000));
        QVERIFY(!restored.dirty());

        QSignalSpy exported(&restored, &VideoEditor::exportSucceeded);
        QSignalSpy failed(&restored, &VideoEditor::exportFailed);
        QVERIFY(restored.exportClip());
        QTRY_VERIFY_WITH_TIMEOUT(exported.count() == 1 || failed.count() > 0, 30000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(exported.count(), 1);
        const QString output = exported.takeFirst().at(0).toString();
        QVERIFY(QFile::exists(output));
        QCOMPARE(library.clipCount(), 1);
        QCOMPARE(library.data(library.index(0), ClipLibraryModel::PathRole).toString(), output);
        QCOMPARE(restored.sourcePath(), output);
        QCOMPARE(fileHash(source), originalHash);

        QString durationOutput;
        QVERIFY(runProcess(ffprobe, {
            QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
            QStringLiteral("format=duration"), QStringLiteral("-of"),
            QStringLiteral("default=noprint_wrappers=1:nokey=1"), output
        }, &durationOutput));
        bool durationOk = false;
        const double outputDuration = durationOutput.toDouble(&durationOk);
        QVERIFY(durationOk);
        QVERIFY(qAbs(outputDuration - 3.0) < 0.15);

        QString audioStreams;
        QVERIFY(runProcess(ffprobe, {
            QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
            QStringLiteral("a:0"), QStringLiteral("-show_entries"), QStringLiteral("stream=codec_type"),
            QStringLiteral("-of"), QStringLiteral("csv=p=0"), output
        }, &audioStreams));
        QCOMPARE(audioStreams, QStringLiteral("audio"));
    }
};

QTEST_MAIN(VideoEditorTest)
#include "video_editor_test.moc"
