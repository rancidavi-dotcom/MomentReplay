#include "windows_screencast.h"

#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QVideoFrame>

WindowsScreenCast::WindowsScreenCast(const QString &, QObject *parent)
    : QObject(parent)
{
    m_session.setScreenCapture(&m_capture);
    m_session.setVideoSink(&m_sink);
    m_firstFrameTimer.setSingleShot(true);
    m_firstFrameTimer.setInterval(8000);
    connect(&m_firstFrameTimer, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("Windows screen capture did not deliver a frame within 8 seconds."));
    });
    connect(&m_capture, &QScreenCapture::errorOccurred, this,
            [this](QScreenCapture::Error error, const QString &message) {
        if (error != QScreenCapture::NoError && (m_starting || m_active))
            fail(message.isEmpty() ? QStringLiteral("Windows screen capture failed.") : message);
    });
    connect(&m_capture, &QScreenCapture::activeChanged, this, [this](bool active) {
        if (!active && (m_starting || m_active))
            fail(QStringLiteral("Windows stopped screen capture. Start capture again to reconnect."));
    });
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (!m_starting && !m_active)
            return;
        const QImage image = frame.toImage();
        if (image.isNull())
            return;
        if (m_starting) {
            m_starting = false;
            m_active = true;
            m_firstFrameTimer.stop();
            emit streamReady(image.size());
            emit activeChanged(true);
        }
        emit frameReady(image, m_clock.nsecsElapsed() / 1000);
    });
}

void WindowsScreenCast::start()
{
    if (m_active || m_starting)
        return;
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        emit failed(QStringLiteral("No display is available for capture."));
        return;
    }
    m_capture.setScreen(screen);
    m_starting = true;
    m_clock.start();
    m_firstFrameTimer.start();
    m_capture.start();
}

void WindowsScreenCast::stop()
{
    const bool wasActive = m_active;
    m_active = false;
    m_starting = false;
    m_firstFrameTimer.stop();
    m_capture.stop();
    if (wasActive)
        emit activeChanged(false);
}

void WindowsScreenCast::fail(const QString &message)
{
    stop();
    emit failed(message);
}
