#include "library/clip_library_model.h"
#include "library/clip_repository.h"
#include "recording/screen_recorder.h"

#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class WindowsCaptureTest final : public QObject {
    Q_OBJECT
private slots:
    void capturesDesktopSavesReplayAndRestarts()
    {
        if (!qEnvironmentVariableIsSet("MOMENT_TEST_DESKTOP_CAPTURE"))
            QSKIP("Enable only on a disposable Windows desktop; this test captures the screen.");
        QLabel demo("Moment — synthetic Windows capture test");
        demo.setStyleSheet("background:#193525;color:#b5ec72;font-size:32px;");
        demo.setAlignment(Qt::AlignCenter);
        demo.showFullScreen();
        QVERIFY(QTest::qWaitForWindowExposed(&demo));
        QTemporaryDir temporary;
        ClipRepository repository(temporary.filePath("library.sqlite3"));
        QVERIFY(repository.isOpen());
        ClipLibraryModel library(repository);
        RecordingPreferences preferences(temporary.filePath("settings.ini"));
        ScreenRecorder recorder(library, preferences, temporary.filePath("videos"),
                                temporary.filePath("pictures"));
        QSignalSpy failures(&recorder, &ScreenRecorder::operationFailed);
        QSignalSpy screenshots(&recorder, &ScreenRecorder::screenshotSaved);
        QSignalSpy replays(&recorder, &ScreenRecorder::replayClipSaved);
        recorder.startBuffering();
        QTRY_VERIFY_WITH_TIMEOUT(recorder.frameAvailable() || !failures.isEmpty(), 12000);
        QVERIFY2(failures.isEmpty(), qPrintable(recorder.statusMessage()));
        QVERIFY(recorder.buffering());
        // Repaint so platforms that suppress unchanged frames still get new samples.
        for (int i = 0; i < 35; ++i) {
            demo.setText(QStringLiteral("Moment — synthetic frame %1").arg(i));
            QTest::qWait(70);
        }
        QTRY_VERIFY(recorder.bufferedSeconds() > 0);
        recorder.saveScreenshot();
        QCOMPARE(screenshots.count(), 1);
        QVERIFY(!QImage(screenshots.first().first().toString()).isNull());
        recorder.saveReplayClip(2);
        QTRY_VERIFY_WITH_TIMEOUT(!replays.isEmpty() || !failures.isEmpty(), 20000);
        QVERIFY2(failures.isEmpty(), qPrintable(recorder.statusMessage()));
        QCOMPARE(replays.count(), 1);
        QVERIFY(QFileInfo(replays.first().first().toString()).size() > 0);
        recorder.stopCapture();
        QVERIFY(!recorder.busy());
        recorder.startBuffering();
        QTRY_VERIFY_WITH_TIMEOUT(recorder.buffering() || !failures.isEmpty(), 12000);
        QVERIFY2(failures.isEmpty(), qPrintable(recorder.statusMessage()));
        recorder.stopCapture();
    }
};

QTEST_MAIN(WindowsCaptureTest)
#include "windows_capture_test.moc"
