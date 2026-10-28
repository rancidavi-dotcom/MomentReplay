#pragma once

#include "../library/clip_library_model.h"
#include "recording_preferences.h"
#include "instant_replay.h"
#include "wayland_screencast.h"

#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVideoFrameInput>
#include <QVideoFrame>
#include <QVideoSink>
#include <QAudioBuffer>
#include <QAudioBufferInput>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <QStringList>

#include <memory>

class ScreenRecorder final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY stateChanged)
    Q_PROPERTY(int bufferedSeconds READ bufferedSeconds NOTIFY bufferedSecondsChanged)
    Q_PROPERTY(int bufferSeconds READ bufferSeconds WRITE setBufferSeconds NOTIFY bufferSecondsChanged)
    Q_PROPERTY(bool replayExporting READ replayExporting NOTIFY stateChanged)
    Q_PROPERTY(bool frameAvailable READ frameAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QStringList audioInputNames READ audioInputNames NOTIFY audioDevicesChanged)
    Q_PROPERTY(int selectedAudioInputIndex READ selectedAudioInputIndex NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString audioCaptureStatus READ audioCaptureStatus NOTIFY audioCaptureStatusChanged)
    Q_PROPERTY(bool audioCaptureActive READ audioCaptureActive NOTIFY audioCaptureStatusChanged)

public:
    explicit ScreenRecorder(ClipLibraryModel &library, RecordingPreferences &preferences,
                            QString outputDirectory, QString screenshotDirectory,
                            QObject *parent = nullptr);

    // Video-frame input is used by deterministic tests. Production uses the system screen source.
    ScreenRecorder(ClipLibraryModel &library, RecordingPreferences &preferences,
                   QString outputDirectory,
                   QString screenshotDirectory,
                   QVideoFrameInput &videoFrameInput, QObject *parent = nullptr);

    bool busy() const;
    bool recording() const;
    bool buffering() const;
    int bufferedSeconds() const;
    int bufferSeconds() const;
    bool replayExporting() const;
    bool frameAvailable() const;
    QString statusMessage() const;
    QStringList audioInputNames() const;
    int selectedAudioInputIndex() const;
    QString audioCaptureStatus() const;
    bool audioCaptureActive() const;

    Q_INVOKABLE void startBuffering();
    Q_INVOKABLE void stopCapture();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void saveReplayClip(int durationSeconds = 15);
    Q_INVOKABLE void saveScreenshot();
    Q_INVOKABLE void setBufferSeconds(int seconds);
    Q_INVOKABLE void setAudioInputIndex(int index);

signals:
    void stateChanged();
    void statusMessageChanged();
    void recordingSaved(const QString &path);
    void replayClipSaved(const QString &path);
    void screenshotSaved(const QString &path);
    void bufferedSecondsChanged();
    void bufferSecondsChanged();
    void audioDevicesChanged();
    void audioCaptureStatusChanged();
    void operationFailed(const QString &message);

private:
    enum class State { Idle, AwaitingScreenPermission, Buffering, Recording, Stopping };
    void initialize();
    void setStatusMessage(const QString &message);
    void fail(const QString &message);
    void finalizeRecording();
    QString makeOutputPath() const;
    QString makeReplayPath() const;
    QString makeScreenshotPath() const;
    void updateLatestFrame(const QImage &image);
    QAudioDevice selectedAudioDevice() const;
    void syncAudioCapture();
    void startAudioCapture();
    void stopAudioCapture();
    void pollAudioSource();
    void queueSessionAudio(const QAudioBuffer &buffer, qint64 timestampUs);
    void pumpSessionAudio();
    void stopRecorderWhenAudioDrained();
    void setAudioCaptureStatus(const QString &message);

    ClipLibraryModel &m_library;
    RecordingPreferences &m_preferences;
    QString m_outputDirectory;
    QString m_screenshotDirectory;
    QString m_statusMessage;
    QImage m_latestFrame;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    QMediaRecorder m_mediaRecorder;
    QMediaDevices m_mediaDevices;
    InstantReplay m_replayBuffer;
    std::unique_ptr<QAudioBufferInput> m_sessionAudioInput;
    std::unique_ptr<QAudioSource> m_audioSource;
    QIODevice *m_audioDevice = nullptr;
    QTimer m_audioPollTimer;
    QAudioFormat m_audioFormat;
    qint64 m_audioOriginMs = 0;
    qint64 m_audioFramesCaptured = 0;
    QList<QAudioBuffer> m_sessionAudioQueue;
    qsizetype m_sessionAudioQueuedBytes = 0;
    qint64 m_sessionAudioOriginUs = 0;
    WaylandScreenCast *m_waylandCapture = nullptr;
    std::unique_ptr<QVideoFrameInput> m_portalFrameInput;
    QVideoFrameInput *m_videoFrameInput = nullptr;
    State m_state = State::Idle;
    QString m_outputPath;
    QVideoFrame m_pendingPortalFrame;
    bool m_stopRequested = false;
    bool m_recordAfterCapture = false;
    bool m_captureActive = false;
    bool m_stopCaptureRequested = false;
    bool m_hasPendingPortalFrame = false;
    bool m_sessionAudioEnabled = false;
    QString m_audioCaptureStatus = QStringLiteral("Audio capture is off");
};
