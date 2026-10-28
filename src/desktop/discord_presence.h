#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

class GameMonitor;

class DiscordPresence final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString activeGameName READ activeGameName NOTIFY activeGameChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit DiscordPresence(GameMonitor &gameMonitor, const QString &settingsFile,
                             QString socketDirectory = {}, QObject *parent = nullptr);

    bool enabled() const;
    QString status() const;
    QString activeGameName() const;
    bool connected() const;

    Q_INVOKABLE void setEnabled(bool enabled);

signals:
    void enabledChanged();
    void statusChanged();
    void activeGameChanged();
    void connectedChanged();

private slots:
    void tryConnect();
    void readAvailableData();

private:
    void setStatus(const QString &status);
    void setConnected(bool connected);
    void sendHandshake();
    void publishActivity();
    void sendFrame(quint32 opcode, const QByteArray &payload);
    QStringList socketDirectories() const;

    GameMonitor &m_gameMonitor;
    QSettings m_settings;
    QLocalSocket m_socket;
    QTimer m_reconnectTimer;
    QByteArray m_incoming;
    QString m_socketDirectory;
    QString m_applicationId;
    QString m_status;
    QString m_lastPublishedActivityKey;
    bool m_enabled = false;
    bool m_connected = false;
    bool m_handshakeComplete = false;
    bool m_hasPublishedActivity = false;
};
