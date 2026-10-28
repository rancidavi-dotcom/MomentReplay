#include "fake_shortcuts_portal.h"
#include "library/clip_library_model.h"
#include "library/clip_repository.h"
#include "recording/global_shortcuts.h"
#include "recording/recording_preferences.h"
#include "recording/screen_recorder.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMediaPlayer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVideoSink>
#include <QWindow>
#include <QtTest>

class BackgroundCaptureTest final : public QObject {
    Q_OBJECT
private slots:
    void globalActionsSavePngAndPlayableReplayWhileHidden()
    {
        qDBusRegisterMetaType<PortalShortcut>();
        qDBusRegisterMetaType<QList<PortalShortcut>>();
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "background_test_portal");
        FakeShortcutsPortal portal;
        QVERIFY(bus.registerVirtualObject("/org/freedesktop/portal/desktop", &portal, QDBusConnection::SubPath));
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        ClipRepository repository(temporary.filePath("library.sqlite3"));
        QVERIFY(repository.isOpen());
        ClipLibraryModel library(repository);
        RecordingPreferences preferences(temporary.filePath("settings.ini"));
        QVideoFrameInput input(QVideoFrameFormat(QSize(320, 180), QVideoFrameFormat::Format_ARGB8888));
        ScreenRecorder recorder(library, preferences, temporary.filePath("videos"),
                                temporary.filePath("pictures"), input);
        GlobalShortcuts shortcuts;
        connect(&shortcuts, &GlobalShortcuts::saveScreenshotRequested, &recorder, &ScreenRecorder::saveScreenshot);
        connect(&shortcuts, &GlobalShortcuts::saveReplayRequested, &recorder, [&] {
            recorder.saveReplayClip(preferences.replayClipSeconds());
        });
        QSignalSpy png(&recorder, &ScreenRecorder::screenshotSaved);
        QSignalSpy mp4(&recorder, &ScreenRecorder::replayClipSaved);
        QSignalSpy errors(&recorder, &ScreenRecorder::operationFailed);

        QWindow moment;
        moment.show();
        QWindow foreground;
        foreground.show();
        foreground.requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), &foreground);
        moment.hide();
        recorder.startBuffering();
        QVERIFY(recorder.buffering());
        QTRY_VERIFY(shortcuts.allRegistered());
        // Generated frames only: no screen, microphone or real portal access.
        for (int i = 0; i < 40; ++i) {
            QImage image(320, 180, QImage::Format_ARGB32);
            image.fill(QColor::fromHsv((i * 17) % 360, 180, 220));
            QVERIFY(input.sendVideoFrame(QVideoFrame(image)));
            QTest::qWait(40);
        }
        QTRY_VERIFY_WITH_TIMEOUT(recorder.bufferedSeconds() > 0, 5000);
        portal.activate(bus, "save-screenshot");
        QTRY_COMPARE(png.count(), 1);
        portal.activate(bus, "save-replay");
        QTRY_COMPARE_WITH_TIMEOUT(mp4.count(), 1, 20000);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(library.clipCount(), 2);
        QVERIFY(!QImage(png.at(0).at(0).toString()).isNull());
        const QString video = mp4.at(0).at(0).toString();
        QVERIFY(QFileInfo(video).size() > 0);
        QVERIFY(!moment.isVisible());
        QCOMPARE(QGuiApplication::focusWindow(), &foreground);
        QVERIFY(recorder.buffering());

        QMediaPlayer player;
        QVideoSink sink;
        QSignalSpy decoded(&sink, &QVideoSink::videoFrameChanged);
        player.setVideoSink(&sink);
        player.setSource(QUrl::fromLocalFile(video));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(decoded.count() > 0, 10000);
        QVERIFY(player.duration() > 0);
        player.stop();
        recorder.stopCapture();
        bus.unregisterService("org.freedesktop.portal.Desktop");
        bus.unregisterObject("/org/freedesktop/portal/desktop", QDBusConnection::UnregisterTree);
    }
};

QTEST_MAIN(BackgroundCaptureTest)
#include "background_capture_test.moc"
