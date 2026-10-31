#pragma once

#include <QElapsedTimer>
#include <QMediaCaptureSession>
#include <QScreenCapture>
#include <QTimer>
#include <QVideoSink>

// Same frame contract as the Linux portal source; the replay/export code is shared.
class WindowsScreenCast final : public QObject {
    Q_OBJECT
public:
    explicit WindowsScreenCast(const QString &unusedRestoreTokenPath, QObject *parent = nullptr);
    void start();
    void stop();
    bool active() const { return m_active; }

signals:
    void activeChanged(bool active);
    void streamReady(const QSize &size);
    void frameReady(const QImage &image, qint64 presentationTimeUs);
    void failed(const QString &message);

private:
    void fail(const QString &message);
    QMediaCaptureSession m_session;
    QScreenCapture m_capture;
    QVideoSink m_sink;
    QTimer m_firstFrameTimer;
    QElapsedTimer m_clock;
    bool m_active = false;
    bool m_starting = false;
};
