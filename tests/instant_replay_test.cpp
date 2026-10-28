#include "../src/recording/instant_replay.h"

#include <QFileInfo>
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioFormat>
#include <QImage>
#include <QMediaPlayer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>

class InstantReplayTest final : public QObject {
    Q_OBJECT

private slots:
    void encodesRecentFramesIntoAPlayableMp4();
    void muxesSelectedAudioIntoReplayClip();
};

void InstantReplayTest::encodesRecentFramesIntoAPlayableMp4()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    InstantReplay replay;
    replay.setBufferSeconds(5);
    QSignalSpy savedSpy(&replay, &InstantReplay::clipSaved);
    QSignalSpy failedSpy(&replay, &InstantReplay::operationFailed);

    QImage image(320, 180, QImage::Format_ARGB32);
    for (int i = 0; i < 36; ++i) {
        image.fill(QColor::fromHsv((i * 11) % 360, 190, 230));
        QVideoFrame frame(image);
        replay.captureFrame(frame);
        QTest::qWait(40);
    }

    QTRY_VERIFY_WITH_TIMEOUT(replay.bufferedSeconds() > 0, 10000);
    const QString output = temporary.filePath(QStringLiteral("Replay.mp4"));
    QVERIFY(replay.saveLastClip(output, 5, 30, 3));
    QTRY_COMPARE_WITH_TIMEOUT(savedSpy.count(), 1, 20000);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(QFileInfo::exists(output));
    QVERIFY(QFileInfo(output).size() > 0);

    QMediaPlayer player;
    QVideoSink sink;
    int decodedFrames = 0;
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &player,
                     [&decodedFrames](const QVideoFrame &frame) {
        if (frame.isValid())
            ++decodedFrames;
    });
    player.setVideoSink(&sink);
    player.setSource(QUrl::fromLocalFile(output));
    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(decodedFrames > 0, 10000);
    QVERIFY(player.duration() > 0);
    player.stop();
}

void InstantReplayTest::muxesSelectedAudioIntoReplayClip()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    InstantReplay replay;
    replay.setBufferSeconds(5);
    QSignalSpy savedSpy(&replay, &InstantReplay::clipSaved);
    QSignalSpy failedSpy(&replay, &InstantReplay::operationFailed);

    QAudioFormat audioFormat;
    audioFormat.setSampleRate(48000);
    audioFormat.setChannelCount(2);
    audioFormat.setSampleFormat(QAudioFormat::Int16);

    QImage image(320, 180, QImage::Format_ARGB32);
    for (int i = 0; i < 36; ++i) {
        const qint64 timestampMs = replay.currentTimestampMs();
        image.fill(QColor::fromHsv((i * 11) % 360, 190, 230));
        replay.captureFrame(QVideoFrame(image));

        QByteArray pcm(1920 * audioFormat.bytesPerFrame(), Qt::Uninitialized);
        auto *samples = reinterpret_cast<qint16 *>(pcm.data());
        for (int frame = 0; frame < 1920; ++frame) {
            constexpr qreal pi = 3.14159265358979323846;
            const qreal phase = 2.0 * pi * 440.0 * frame / audioFormat.sampleRate();
            const qint16 sample = static_cast<qint16>(std::sin(phase) * 7000.0);
            samples[frame * 2] = sample;
            samples[frame * 2 + 1] = sample;
        }
        replay.captureAudioBuffer(QAudioBuffer(pcm, audioFormat), timestampMs * 1000);
        QTest::qWait(40);
    }

    QTRY_VERIFY_WITH_TIMEOUT(replay.bufferedSeconds() > 0, 10000);
    QVERIFY(replay.audioBuffered());
    const QString output = temporary.filePath(QStringLiteral("Replay-with-audio.mp4"));
    QVERIFY(replay.saveLastClip(output, 5, 30, 3));
    QTRY_COMPARE_WITH_TIMEOUT(savedSpy.count(), 1, 20000);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(QFileInfo(output).size() > 0);

    QMediaPlayer player;
    QVideoSink videoSink;
    QAudioBufferOutput audioOutput;
    int decodedAudioBuffers = 0;
    QObject::connect(&audioOutput, &QAudioBufferOutput::audioBufferReceived, &player,
                     [&decodedAudioBuffers](const QAudioBuffer &buffer) {
        if (buffer.isValid() && buffer.frameCount() > 0)
            ++decodedAudioBuffers;
    });
    player.setVideoSink(&videoSink);
    player.setAudioBufferOutput(&audioOutput);
    player.setSource(QUrl::fromLocalFile(output));
    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(decodedAudioBuffers > 0, 10000);
    player.stop();
}

QTEST_MAIN(InstantReplayTest)
#include "instant_replay_test.moc"
