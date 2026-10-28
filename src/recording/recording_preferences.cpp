#include "recording_preferences.h"

RecordingPreferences::RecordingPreferences(const QString &settingsFile, QObject *parent)
    : QObject(parent), m_settings(settingsFile, QSettings::IniFormat)
{
    m_frameRate = m_settings.value(QStringLiteral("recording/frameRate"), 30).toInt();
    if (m_frameRate != 30 && m_frameRate != 60)
        m_frameRate = 30;

    m_quality = m_settings.value(QStringLiteral("recording/quality"), 3).toInt();
    if (m_quality < 0 || m_quality > 4)
        m_quality = 3;

    m_replaySeconds = m_settings.value(QStringLiteral("recording/replaySeconds"), 30).toInt();
    if (m_replaySeconds < 5 || m_replaySeconds > 120)
        m_replaySeconds = 30;

    m_replayClipSeconds = m_settings.value(QStringLiteral("recording/replayClipSeconds"), 15).toInt();
    if (m_replayClipSeconds != 15 && m_replayClipSeconds != 30
        && m_replayClipSeconds != 60 && m_replayClipSeconds != 90
        && m_replayClipSeconds != 120)
        m_replayClipSeconds = 15;

    m_captureAudio = m_settings.value(QStringLiteral("recording/captureAudio"), false).toBool();
    m_audioInputId = m_settings.value(QStringLiteral("recording/audioInputId")).toByteArray();
}

int RecordingPreferences::frameRate() const
{
    return m_frameRate;
}

int RecordingPreferences::quality() const
{
    return m_quality;
}

int RecordingPreferences::replaySeconds() const
{
    return m_replaySeconds;
}

int RecordingPreferences::replayClipSeconds() const
{
    return m_replayClipSeconds;
}

bool RecordingPreferences::captureAudio() const
{
    return m_captureAudio;
}

QByteArray RecordingPreferences::audioInputId() const
{
    return m_audioInputId;
}

QString RecordingPreferences::settingsFilePath() const
{
    return m_settings.fileName();
}

void RecordingPreferences::setFrameRate(int value)
{
    if ((value != 30 && value != 60) || value == m_frameRate)
        return;
    m_frameRate = value;
    m_settings.setValue(QStringLiteral("recording/frameRate"), m_frameRate);
    m_settings.sync();
    emit frameRateChanged();
}

void RecordingPreferences::setQuality(int value)
{
    if (value < 0 || value > 4 || value == m_quality)
        return;
    m_quality = value;
    m_settings.setValue(QStringLiteral("recording/quality"), m_quality);
    m_settings.sync();
    emit qualityChanged();
}

void RecordingPreferences::setReplaySeconds(int value)
{
    if (value < 5 || value > 120 || value == m_replaySeconds)
        return;
    m_replaySeconds = value;
    m_settings.setValue(QStringLiteral("recording/replaySeconds"), m_replaySeconds);
    m_settings.sync();
    emit replaySecondsChanged();
}

void RecordingPreferences::setReplayClipSeconds(int value)
{
    if ((value != 15 && value != 30 && value != 60 && value != 90 && value != 120)
        || value == m_replayClipSeconds)
        return;
    m_replayClipSeconds = value;
    m_settings.setValue(QStringLiteral("recording/replayClipSeconds"), m_replayClipSeconds);
    m_settings.sync();
    emit replayClipSecondsChanged();
}

void RecordingPreferences::setCaptureAudio(bool value)
{
    if (value == m_captureAudio)
        return;
    m_captureAudio = value;
    m_settings.setValue(QStringLiteral("recording/captureAudio"), value);
    m_settings.sync();
    emit captureAudioChanged();
}

void RecordingPreferences::setAudioInputId(const QByteArray &value)
{
    if (value == m_audioInputId)
        return;
    m_audioInputId = value;
    m_settings.setValue(QStringLiteral("recording/audioInputId"), value);
    m_settings.sync();
    emit audioInputIdChanged();
}
