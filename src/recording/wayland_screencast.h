#pragma once

#include <QDBusConnection>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

typedef struct _GstElement GstElement;
class QImage;
class QTimer;

class WaylandScreenCast final : public QObject {
    Q_OBJECT

public:
    explicit WaylandScreenCast(QString restoreTokenPath, QObject *parent = nullptr);
    ~WaylandScreenCast() override;

    void start();
    void stop();
    bool active() const;

signals:
    void activeChanged(bool active);
    void streamReady(const QSize &size);
    void frameReady(const QImage &image, qint64 presentationTimeUs);
    void failed(const QString &message);

private slots:
    void handlePortalResponse(uint response, const QVariantMap &results);
    void checkPipeWireBus();

private:
    enum class RequestStage { None, CreateSession, SelectSources, Start };
    void callPortal(const QString &method, const QVariantList &arguments,
                    RequestStage stage);
    void openPipeWire(const QVariantMap &results);
    bool startPipeWirePipeline(quint32 nodeId, const QVariantMap &properties);
    void stopPipeWirePipeline();
    void closePortalSession();
    QString readRestoreToken() const;
    void saveRestoreToken(const QString &token);
    void clearRestoreToken();
    void fail(const QString &message);
    QDBusConnection m_bus;
    QString m_restoreTokenPath;
    QString m_requestPath;
    QString m_sessionPath;
    QString m_restoreToken;
    RequestStage m_requestStage = RequestStage::None;
    QDBusUnixFileDescriptor m_pipeWireFd;
    QTimer *m_busTimer = nullptr;
    GstElement *m_pipeline = nullptr;
    bool m_active = false;
    bool m_cancelled = false;
    bool m_receivedFrame = false;
    QElapsedTimer m_captureStartedAt;
    quint32 m_portalVersion = 0;
};
