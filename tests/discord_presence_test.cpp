#include "../src/desktop/discord_presence.h"
#include "../src/recording/game_monitor.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {
struct RpcFrame {
    quint32 opcode = std::numeric_limits<quint32>::max();
    QByteArray payload;
};

QByteArray readExactly(QLocalSocket &socket, qsizetype length)
{
    QByteArray result;
    while (result.size() < length) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(3000))
            return {};
        result.append(socket.read(length - result.size()));
    }
    return result;
}

RpcFrame readFrame(QLocalSocket &socket)
{
    const QByteArray header = readExactly(socket, 8);
    if (header.size() != 8)
        return {};
    const auto *bytes = reinterpret_cast<const uchar *>(header.constData());
    const quint32 opcode = qFromLittleEndian<quint32>(bytes);
    const quint32 size = qFromLittleEndian<quint32>(bytes + 4);
    return {opcode, readExactly(socket, size)};
}

void writeFrame(QLocalSocket &socket, quint32 opcode, const QByteArray &payload)
{
    QByteArray frame(8, Qt::Uninitialized);
    qToLittleEndian<quint32>(opcode, reinterpret_cast<uchar *>(frame.data()));
    qToLittleEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(frame.data() + 4));
    frame.append(payload);
    socket.write(frame);
    socket.flush();
}

bool createGameProcess(const QString &procRoot, const QString &executablePath)
{
    const QString processDirectory = procRoot + QStringLiteral("/4242");
    if (!QDir().mkpath(processDirectory) || !QDir().mkpath(QFileInfo(executablePath).path()))
        return false;
    QFile executable(executablePath);
    if (!executable.open(QIODevice::WriteOnly))
        return false;
    executable.write("demo game process");
    executable.close();
    if (!QFile::link(executablePath, processDirectory + QStringLiteral("/exe")))
        return false;
    QFile comm(processDirectory + QStringLiteral("/comm"));
    if (!comm.open(QIODevice::WriteOnly))
        return false;
    comm.write("demo-game\n");
    return true;
}

QJsonObject readActivity(QLocalSocket &serverSocket)
{
    const RpcFrame frame = readFrame(serverSocket);
    if (frame.opcode != 1)
        return {};
    const QJsonObject command = QJsonDocument::fromJson(frame.payload).object();
    if (command.value(QStringLiteral("cmd")).toString() != QStringLiteral("SET_ACTIVITY"))
        return {};
    return command.value(QStringLiteral("args")).toObject()
        .value(QStringLiteral("activity")).toObject();
}
}

class DiscordPresenceTest final : public QObject {
    Q_OBJECT
private slots:
    void publishesMomentOpenAndAutomaticallyDetectedGame();
};

void DiscordPresenceTest::publishesMomentOpenAndAutomaticallyDetectedGame()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString procRoot = temporary.filePath(QStringLiteral("proc"));
    const QString socketDirectory = temporary.filePath(QStringLiteral("discord"));
    QVERIFY(QDir().mkpath(procRoot));
    QVERIFY(QDir().mkpath(socketDirectory));

    QLocalServer server;
    QVERIFY2(server.listen(QDir(socketDirectory).filePath(QStringLiteral("discord-ipc-0"))),
             qPrintable(server.errorString()));
    const QString settingsPath = temporary.filePath(QStringLiteral("settings.ini"));
    GameMonitor monitor(settingsPath, procRoot);
    DiscordPresence presence(monitor, settingsPath, socketDirectory);

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
    QLocalSocket *rpcSocket = server.nextPendingConnection();
    QVERIFY(rpcSocket);
    const RpcFrame handshakeFrame = readFrame(*rpcSocket);
    QCOMPARE(handshakeFrame.opcode, quint32(0));
    const QJsonObject handshake = QJsonDocument::fromJson(handshakeFrame.payload).object();
    QCOMPARE(handshake.value(QStringLiteral("client_id")).toString(),
             QStringLiteral(MOMENT_DISCORD_APPLICATION_ID));

    const QByteArray ready = QJsonDocument(QJsonObject{
        {QStringLiteral("cmd"), QStringLiteral("DISPATCH")},
        {QStringLiteral("evt"), QStringLiteral("READY")},
        {QStringLiteral("data"), QJsonObject{}}
    }).toJson(QJsonDocument::Compact);
    writeFrame(*rpcSocket, 1, ready);
    QTRY_COMPARE_WITH_TIMEOUT(presence.status(),
                              QStringLiteral("Sharing “Moment aberto” with Discord"), 3000);

    QJsonObject activity = readActivity(*rpcSocket);
    QCOMPARE(activity.value(QStringLiteral("details")).toString(), QStringLiteral("Moment aberto"));
    QVERIFY(!activity.contains(QStringLiteral("state")));

    const QString gameExecutable = temporary.filePath(
        QStringLiteral("home/player/Games/Heroic/Sample Adventure/bin/demo-game"));
    QVERIFY(createGameProcess(procRoot, gameExecutable));
    monitor.refresh();
    QCOMPARE(monitor.presenceTargetName(), QStringLiteral("Sample Adventure"));

    activity = readActivity(*rpcSocket);
    QCOMPARE(activity.value(QStringLiteral("details")).toString(),
             QStringLiteral("Jogando Sample Adventure com Moment"));

    QVERIFY(QFile::remove(procRoot + QStringLiteral("/4242/exe")));
    QVERIFY(QFile::remove(procRoot + QStringLiteral("/4242/comm")));
    monitor.refresh();
    QVERIFY(monitor.presenceTargetName().isEmpty());
    activity = readActivity(*rpcSocket);
    QCOMPARE(activity.value(QStringLiteral("details")).toString(), QStringLiteral("Moment aberto"));
}

QTEST_GUILESS_MAIN(DiscordPresenceTest)
#include "discord_presence_test.moc"
