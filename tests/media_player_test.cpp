#include <QMediaPlayer>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtTest>

class MediaPlayerTest final : public QObject {
    Q_OBJECT

private slots:
    void decodesSyntheticVideoFrames();
};

void MediaPlayerTest::decodesSyntheticVideoFrames()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("FFmpeg is unavailable; skipping the optional media decode integration test.");

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString videoPath = temporary.path() + QStringLiteral("/generated-fixture.mp4");

    QProcess generator;
    generator.start(ffmpeg, {
        QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("testsrc=size=160x90:rate=10:duration=2"), QStringLiteral("-an"),
        QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("5"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"), videoPath
    });
    QVERIFY(generator.waitForStarted());
    QVERIFY(generator.waitForFinished(15000));
    QCOMPARE(generator.exitStatus(), QProcess::NormalExit);
    QCOMPARE(generator.exitCode(), 0);

    QMediaPlayer player;
    QVideoSink sink;
    int validFrames = 0;
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &player,
                     [&validFrames](const QVideoFrame &frame) {
        if (frame.isValid())
            ++validFrames;
    });
    player.setVideoSink(&sink);
    player.setSource(QUrl::fromLocalFile(videoPath));
    player.play();

    QTRY_VERIFY_WITH_TIMEOUT(validFrames > 0, 10000);
    QVERIFY(player.duration() > 0);
    player.stop();
}

QTEST_MAIN(MediaPlayerTest)
#include "media_player_test.moc"
