#include "../src/recording/recording_preferences.h"

#include <QTemporaryDir>
#include <QtTest>

class RecordingPreferencesTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsSupportedValuesAcrossInstances();
    void rejectsUnsupportedValues();
};

void RecordingPreferencesTest::persistsSupportedValuesAcrossInstances()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("settings.ini"));

    {
        RecordingPreferences preferences(path);
        QCOMPARE(preferences.frameRate(), 30);
        QCOMPARE(preferences.quality(), 3);
        QCOMPARE(preferences.replaySeconds(), 30);
        QCOMPARE(preferences.replayClipSeconds(), 15);
        preferences.setFrameRate(60);
        preferences.setQuality(4);
        preferences.setReplaySeconds(60);
        preferences.setReplayClipSeconds(60);
        preferences.setCaptureAudio(true);
        preferences.setAudioInputId(QByteArrayLiteral("device-42"));
    }

    RecordingPreferences restored(path);
    QCOMPARE(restored.frameRate(), 60);
    QCOMPARE(restored.quality(), 4);
    QCOMPARE(restored.replaySeconds(), 60);
    QCOMPARE(restored.replayClipSeconds(), 60);
    QVERIFY(restored.captureAudio());
    QCOMPARE(restored.audioInputId(), QByteArrayLiteral("device-42"));
}

void RecordingPreferencesTest::rejectsUnsupportedValues()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    RecordingPreferences preferences(temporary.filePath(QStringLiteral("settings.ini")));
    QSignalSpy frameRateSpy(&preferences, &RecordingPreferences::frameRateChanged);
    QSignalSpy qualitySpy(&preferences, &RecordingPreferences::qualityChanged);
    QSignalSpy replaySpy(&preferences, &RecordingPreferences::replaySecondsChanged);
    QSignalSpy replayClipSpy(&preferences, &RecordingPreferences::replayClipSecondsChanged);

    preferences.setFrameRate(24);
    preferences.setQuality(5);
    preferences.setReplaySeconds(121);
    preferences.setReplayClipSeconds(45);

    QCOMPARE(preferences.frameRate(), 30);
    QCOMPARE(preferences.quality(), 3);
    QCOMPARE(preferences.replaySeconds(), 30);
    QCOMPARE(preferences.replayClipSeconds(), 15);
    QCOMPARE(frameRateSpy.count(), 0);
    QCOMPARE(qualitySpy.count(), 0);
    QCOMPARE(replaySpy.count(), 0);
    QCOMPARE(replayClipSpy.count(), 0);
}

QTEST_MAIN(RecordingPreferencesTest)
#include "recording_preferences_test.moc"
