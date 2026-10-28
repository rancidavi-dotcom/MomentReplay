#pragma once

#include <QObject>
#include <QString>
#include <QSettings>

class RecordingPreferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY qualityChanged)
    Q_PROPERTY(int replaySeconds READ replaySeconds WRITE setReplaySeconds NOTIFY replaySecondsChanged)
    Q_PROPERTY(int replayClipSeconds READ replayClipSeconds WRITE setReplayClipSeconds NOTIFY replayClipSecondsChanged)
    Q_PROPERTY(bool captureAudio READ captureAudio WRITE setCaptureAudio NOTIFY captureAudioChanged)
    Q_PROPERTY(QByteArray audioInputId READ audioInputId WRITE setAudioInputId NOTIFY audioInputIdChanged)

public:
    explicit RecordingPreferences(const QString &settingsFile, QObject *parent = nullptr);

    int frameRate() const;
    int quality() const;
    int replaySeconds() const;
    int replayClipSeconds() const;
    bool captureAudio() const;
    QByteArray audioInputId() const;
    QString settingsFilePath() const;

    Q_INVOKABLE void setFrameRate(int value);
    Q_INVOKABLE void setQuality(int value);
    Q_INVOKABLE void setReplaySeconds(int value);
    Q_INVOKABLE void setReplayClipSeconds(int value);
    Q_INVOKABLE void setCaptureAudio(bool value);
    Q_INVOKABLE void setAudioInputId(const QByteArray &value);

signals:
    void frameRateChanged();
    void qualityChanged();
    void replaySecondsChanged();
    void replayClipSecondsChanged();
    void captureAudioChanged();
    void audioInputIdChanged();

private:
    QSettings m_settings;
    int m_frameRate = 30;
    int m_quality = 3;
    int m_replaySeconds = 30;
    int m_replayClipSeconds = 15;
    bool m_captureAudio = false;
    QByteArray m_audioInputId;
};
