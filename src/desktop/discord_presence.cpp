#include "discord_presence.h"

#include "../recording/game_monitor.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <QtEndian>

#include <unistd.h>

namespace {
constexpr auto kEnabledKey = "discord/richPresenceEnabled";
constexpr quint32 kHandshakeOpcode = 0;
constexpr quint32 kFrameOpcode = 1;
constexpr quint32 kMaximumPayloadSize = 1024 * 1024;
#ifndef MOMENT_DISCORD_APPLICATION_ID
#define MOMENT_DISCORD_APPLICATION_ID ""
#endif
const QString kMomentApplicationId = QStringLiteral(MOMENT_DISCORD_APPLICATION_ID);
}

DiscordPresence::DiscordPresence(GameMonitor &gameMonitor, const QString &settingsFile,
                                 QString socketDirectory, QObject *parent)
    : QObject(parent), m_gameMonitor(gameMonitor),
      m_settings(settingsFile, QSettings::IniFormat), m_socket(this),
      m_reconnectTimer(this), m_socketDirectory(std::move(socketDirectory)),
      m_applicationId(kMomentApplicationId),
      m_enabled(m_settings.value(kEnabledKey, true).toBool())
{
    m_reconnectTimer.setInterval(3000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &DiscordPresence::tryConnect);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        setConnected(true);
        m_handshakeComplete = false;
        m_incoming.clear();
        sendHandshake();
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &DiscordPresence::readAvailableData);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        m_handshakeComplete = false;
        m_hasPublishedActivity = false;
        m_lastPublishedActivityKey.clear();
        setConnected(false);
        if (m_enabled && !m_applicationId.isEmpty()) {
            setStatus(QStringLiteral("Discord disconnected; retrying"));
            if (!m_reconnectTimer.isActive())
                m_reconnectTimer.start();
        }
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (m_socket.state() == QLocalSocket::UnconnectedState) {
            setConnected(false);
            if (m_enabled && !m_applicationId.isEmpty()) {
                setStatus(QStringLiteral("Discord desktop not found; retrying"));
                if (!m_reconnectTimer.isActive())
                    m_reconnectTimer.start();
            }
        }
    });
    connect(&m_gameMonitor, &GameMonitor::activeGameChanged, this, [this] {
        emit activeGameChanged();
    });
    connect(&m_gameMonitor, &GameMonitor::presenceTargetChanged, this, [this] {
        emit activeGameChanged();
        publishActivity();
    });

    if (m_enabled && !m_applicationId.isEmpty()) {
        setStatus(QStringLiteral("Looking for Discord desktop"));
        tryConnect();
        if (!m_connected)
            m_reconnectTimer.start();
    } else if (m_applicationId.isEmpty()) {
        setStatus(QStringLiteral("Automatic presence is waiting for Moment's Discord app registration"));
    } else {
        setStatus(QStringLiteral("Rich Presence is off"));
    }
}

bool DiscordPresence::enabled() const
{
    return m_enabled;
}

QString DiscordPresence::status() const
{
    return m_status;
}

QString DiscordPresence::activeGameName() const
{
    return m_gameMonitor.presenceTargetName();
}

bool DiscordPresence::connected() const
{
    return m_connected;
}

void DiscordPresence::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_settings.setValue(kEnabledKey, enabled);
    m_settings.sync();
    emit enabledChanged();
    if (!enabled) {
        m_reconnectTimer.stop();
        if (m_handshakeComplete)
            publishActivity();
        m_handshakeComplete = false;
        m_hasPublishedActivity = false;
        m_lastPublishedActivityKey.clear();
        if (m_socket.state() != QLocalSocket::UnconnectedState)
            m_socket.disconnectFromServer();
        setConnected(false);
        setStatus(QStringLiteral("Rich Presence is off"));
        return;
    }
    if (m_applicationId.isEmpty()) {
        setStatus(QStringLiteral("Automatic presence is waiting for Moment's Discord app registration"));
        return;
    }
    setStatus(QStringLiteral("Looking for Discord desktop"));
    tryConnect();
    if (!m_connected)
        m_reconnectTimer.start();
}

void DiscordPresence::tryConnect()
{
    if (!m_enabled || m_applicationId.isEmpty() || m_socket.state() != QLocalSocket::UnconnectedState)
        return;

    for (const QString &directory : socketDirectories()) {
        for (int index = 0; index < 10; ++index) {
            const QString path = QDir(directory).filePath(QStringLiteral("discord-ipc-%1").arg(index));
            if (!QFileInfo::exists(path))
                continue;
            m_socket.connectToServer(path, QIODevice::ReadWrite);
            if (m_socket.waitForConnected(120)) {
                m_reconnectTimer.stop();
                return;
            }
            m_socket.abort();
        }
    }
    setStatus(QStringLiteral("Discord desktop not found; retrying"));
}

void DiscordPresence::readAvailableData()
{
    m_incoming.append(m_socket.readAll());
    while (m_incoming.size() >= 8) {
        const auto *header = reinterpret_cast<const uchar *>(m_incoming.constData());
        const quint32 opcode = qFromLittleEndian<quint32>(header);
        const quint32 payloadLength = qFromLittleEndian<quint32>(header + 4);
        if (payloadLength > kMaximumPayloadSize) {
            m_socket.abort();
            setStatus(QStringLiteral("Discord sent an invalid RPC frame"));
            return;
        }
        const qsizetype frameLength = 8 + qsizetype(payloadLength);
        if (m_incoming.size() < frameLength)
            return;
        const QByteArray payload = m_incoming.mid(8, payloadLength);
        m_incoming.remove(0, frameLength);
        if (opcode == 1) {
            const QJsonObject message = QJsonDocument::fromJson(payload).object();
            const QString event = message.value(QStringLiteral("evt")).toString();
            if (event == QStringLiteral("READY")) {
                m_handshakeComplete = true;
                setStatus(QStringLiteral("Connected to Discord"));
                publishActivity();
            } else if (event == QStringLiteral("ERROR")) {
                const QJsonObject data = message.value(QStringLiteral("data")).toObject();
                setStatus(data.value(QStringLiteral("message")).toString(QStringLiteral("Discord rejected Rich Presence")));
            }
        }
    }
}

void DiscordPresence::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void DiscordPresence::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}

void DiscordPresence::sendHandshake()
{
    const QJsonObject handshake{
        {QStringLiteral("v"), 1},
        {QStringLiteral("client_id"), m_applicationId}
    };
    sendFrame(kHandshakeOpcode, QJsonDocument(handshake).toJson(QJsonDocument::Compact));
}

void DiscordPresence::publishActivity()
{
    if (!m_handshakeComplete)
        return;

    const QString targetName = m_enabled ? m_gameMonitor.presenceTargetName() : QString();
    const QString activityKey = !m_enabled ? QStringLiteral("disabled")
        : targetName.isEmpty() ? QStringLiteral("moment-open")
                               : QStringLiteral("target:%1").arg(targetName);
    if (m_hasPublishedActivity && activityKey == m_lastPublishedActivityKey)
        return;

    QJsonObject activity;
    if (m_enabled) {
        const QString details = targetName.isEmpty()
            ? QStringLiteral("Moment aberto")
            : QStringLiteral("Jogando %1 com Moment").arg(targetName.left(80));
        activity = {
            {QStringLiteral("type"), 0},
            {QStringLiteral("details"), details}
        };
    }
    const QJsonObject arguments{
        {QStringLiteral("pid"), qint64(::getpid())},
        {QStringLiteral("activity"), m_enabled ? QJsonValue(activity) : QJsonValue(QJsonValue::Null)}
    };
    const QJsonObject command{
        {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("nonce"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("args"), arguments}
    };
    sendFrame(kFrameOpcode, QJsonDocument(command).toJson(QJsonDocument::Compact));
    m_lastPublishedActivityKey = activityKey;
    m_hasPublishedActivity = true;
    if (!m_enabled)
        setStatus(QStringLiteral("Rich Presence is off"));
    else if (targetName.isEmpty())
        setStatus(QStringLiteral("Sharing “Moment aberto” with Discord"));
    else
        setStatus(QStringLiteral("Sharing “Jogando %1 com Moment” with Discord").arg(targetName));
}

void DiscordPresence::sendFrame(quint32 opcode, const QByteArray &payload)
{
    if (m_socket.state() != QLocalSocket::ConnectedState)
        return;
    QByteArray frame(8, Qt::Uninitialized);
    qToLittleEndian<quint32>(opcode, reinterpret_cast<uchar *>(frame.data()));
    qToLittleEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(frame.data() + 4));
    frame.append(payload);
    m_socket.write(frame);
    m_socket.flush();
}

QStringList DiscordPresence::socketDirectories() const
{
    QStringList directories;
    if (!m_socketDirectory.isEmpty())
        directories.append(m_socketDirectory);
    const QString runtimeDirectory = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDirectory.isEmpty() && !directories.contains(runtimeDirectory))
        directories.append(runtimeDirectory);
    const QString tempDirectory = qEnvironmentVariable("TMPDIR");
    if (!tempDirectory.isEmpty() && !directories.contains(tempDirectory))
        directories.append(tempDirectory);
    if (!directories.contains(QDir::tempPath()))
        directories.append(QDir::tempPath());
    if (!directories.contains(QStringLiteral("/tmp")))
        directories.append(QStringLiteral("/tmp"));
    return directories;
}
