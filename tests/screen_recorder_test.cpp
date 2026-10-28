#include "../src/library/clip_library_model.h"
#include "../src/library/clip_repository.h"
#include "../src/recording/screen_recorder.h"
#include "../src/recording/recording_preferences.h"

#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QVideoFrame>
#include <QtTest>

class ScreenRecorderTest final : public QObject {
    Q_OBJECT

private slots:
    void recordsInjectedFramesAndAddsFinalizedVideoToLibrary();
    void savesCapturedScreenshotAndAddsImageToLibrary();
};

void ScreenRecorderTest::recordsInjectedFramesAndAddsFinalizedVideoToLibrary()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ClipRepository repository(temporary.filePath(QStringLiteral("library.sqlite3")));
    QVERIFY2(repository.isOpen(), qPrintable(repository.errorString()));
    ClipLibraryModel library(repository);
    RecordingPreferences preferences(temporary.filePath(QStringLiteral("settings.ini")));
    const QVideoFrameFormat frameFormat(QSize(320, 180), QVideoFrameFormat::Format_ARGB8888);
    QVideoFrameInput input(frameFormat);
    ScreenRecorder recorder(library, preferences, temporary.filePath(QStringLiteral("videos")),
                            temporary.filePath(QStringLiteral("pictures")), input);
    QSignalSpy savedSpy(&recorder, &ScreenRecorder::recordingSaved);
    QSignalSpy failedSpy(&recorder, &ScreenRecorder::operationFailed);

    recorder.startRecording();
    QTRY_VERIFY_WITH_TIMEOUT(recorder.recording(), 10000);

    QImage image(320, 180, QImage::Format_ARGB32);
    for (int frameNumber = 0; frameNumber < 45; ++frameNumber) {
        image.fill(QColor::fromHsv((frameNumber * 7) % 360, 180, 220));
        QVideoFrame frame(image);
        frame.setStartTime(frameNumber * 33333);
        frame.setEndTime((frameNumber + 1) * 33333);
        if (!input.sendVideoFrame(frame))
            QTest::qWait(25);
        else
            QTest::qWait(34);
    }

    recorder.stopRecording();
    QTRY_COMPARE_WITH_TIMEOUT(savedSpy.count(), 1, 15000);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(library.clipCount(), 1);
    const QString savedPath = savedSpy.takeFirst().at(0).toString();
    const QFileInfo savedFile(savedPath);
    QVERIFY(savedFile.exists());
    QVERIFY(savedFile.size() > 0);
    QVERIFY(savedPath.endsWith(QStringLiteral(".mp4")));
}

void ScreenRecorderTest::savesCapturedScreenshotAndAddsImageToLibrary()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    ClipRepository repository(temporary.filePath(QStringLiteral("library.sqlite3")));
    QVERIFY2(repository.isOpen(), qPrintable(repository.errorString()));
    ClipLibraryModel library(repository);
    RecordingPreferences preferences(temporary.filePath(QStringLiteral("settings.ini")));
    const QVideoFrameFormat frameFormat(QSize(160, 90), QVideoFrameFormat::Format_ARGB8888);
    QVideoFrameInput input(frameFormat);
    ScreenRecorder recorder(library, preferences, temporary.filePath(QStringLiteral("videos")),
                            temporary.filePath(QStringLiteral("pictures")), input);
    QSignalSpy screenshotSpy(&recorder, &ScreenRecorder::screenshotSaved);
    QSignalSpy failedSpy(&recorder, &ScreenRecorder::operationFailed);

    recorder.startBuffering();
    QVERIFY(recorder.buffering());
    QImage image(160, 90, QImage::Format_ARGB32);
    image.fill(QColor(40, 170, 210));
    QVideoFrame frame(image);
    QVERIFY(input.sendVideoFrame(frame));
    QTRY_VERIFY_WITH_TIMEOUT(recorder.frameAvailable(), 3000);

    recorder.saveScreenshot();
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(screenshotSpy.count(), 1);
    QCOMPARE(library.clipCount(), 1);
    const QString path = screenshotSpy.takeFirst().at(0).toString();
    const QImage saved(path);
    QVERIFY(!saved.isNull());
    QCOMPARE(saved.size(), image.size());
    QVERIFY(library.data(library.index(0, 0), ClipLibraryModel::ImageRole).toBool());
}

QTEST_MAIN(ScreenRecorderTest)
#include "screen_recorder_test.moc"
