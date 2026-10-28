#pragma once

#include <QFutureWatcher>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QObject>
#include <QVideoFrame>
#include <QVideoFrameInput>
#include <QVector>
#include <QElapsedTimer>
#include <QAudioBuffer>
#include <QAudioBufferInput>
#include <QAudioFormat>

class InstantReplay final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int bufferSeconds READ bufferSeconds WRITE setBufferSeconds NOTIFY bufferSecondsChanged)
    Q_PROPERTY(int bufferedSeconds READ bufferedSeconds NOTIFY bufferedSecondsChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool audioBuffered READ audioBuffered NOTIFY audioBufferedChanged)

public:
    explicit InstantReplay(QObject *parent = nullptr);

    int bufferSeconds() const;
    int bufferedSeconds() const;
    bool exporting() const;
    QString statusMessage() const;
    bool audioBuffered() const;
    qint64 currentTimestampMs() const;

    void captureFrame(const QVideoFrame &frame);
    void captureAudioBuffer(const QAudioBuffer &buffer, qint64 timestampUs);
    void clearAudioBuffer();
    void setFrameRate(int frameRate);
    Q_INVOKABLE bool saveLastClip(const QString &outputPath, int durationSeconds,
                                  int frameRate, int quality);
    Q_INVOKABLE void setBufferSeconds(int seconds);

signals:
    void bufferSecondsChanged();
    void bufferedSecondsChanged();
    void exportingChanged();
    void statusMessageChanged();
    void audioBufferedChanged();
    void clipSaved(const QString &path);
    void operationFailed(const QString &message);

private:
    struct Frame {
        QByteArray jpeg;
        qint64 timestampMs = 0;
        QSize size;
    };

    struct PendingFrame {
        QVideoFrame frame;
        qint64 timestampMs = 0;
    };

    struct AudioChunk {
        QByteArray pcm;
        QAudioFormat format;
        qint64 timestampUs = 0;
        int frameCount = 0;
    };

    static Frame encodeFrame(const QVideoFrame &frame, qint64 timestampMs);
    void startEncodingPendingFrame();
    void appendEncodedFrame(Frame frame);
    void trimBuffer();
    void sendExportFrames();
    void sendExportAudio();
    void maybeFinishExport();
    void finishExport();
    void failExport(const QString &message);
    void setStatusMessage(const QString &message);

    QElapsedTimer m_clock;
    QFutureWatcher<Frame> m_frameEncoder;
    QVector<Frame> m_frames;
    QVector<AudioChunk> m_audioChunks;
    PendingFrame m_pendingFrame;
    qint64 m_lastAcceptedTimestampMs = -1;
    qsizetype m_bufferBytes = 0;
    qsizetype m_maxBufferBytes = 128 * 1024 * 1024;
    int m_bufferSeconds = 30;
    int m_frameRate = 30;
    int m_exportIndex = 0;
    int m_exportFps = 30;
    int m_audioExportIndex = 0;
    qint64 m_exportOriginMs = 0;
    bool m_encodingFrame = false;
    bool m_exporting = false;
    bool m_finalizeRequested = false;
    bool m_videoInputFinished = false;
    bool m_audioInputFinished = true;
    QString m_statusMessage = QStringLiteral("Replay buffer is waiting for capture");
    QString m_outputPath;
    QVector<Frame> m_exportFrames;
    QMediaCaptureSession m_exportSession;
    QMediaRecorder m_exportRecorder;
    QVideoFrameInput *m_exportInput = nullptr;
    QAudioBufferInput *m_exportAudioInput = nullptr;
    QAudioFormat m_audioFormat;
    qsizetype m_audioBufferBytes = 0;
    qsizetype m_maxAudioBufferBytes = 32 * 1024 * 1024;
    QVector<QAudioBuffer> m_exportAudioBuffers;
};
