#include "../src/recording/game_monitor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class GameMonitorTest final : public QObject {
    Q_OBJECT

private slots:
    void detectsConfiguredGameAndRestoresItsProfile();
    void detectsLauncherWithoutNeedingAGameProfile_data();
    void detectsLauncherWithoutNeedingAGameProfile();
    void detectsInstalledGameAutomaticallyAndPrioritizesItOverLauncher();
};

namespace {
bool createProcess(const QString &procRoot, const QString &pid, const QString &executablePath,
                   const QString &comm, const QString &commandLine = {})
{
    const QString processDirectory = procRoot + QLatin1Char('/') + pid;
    if (!QDir().mkpath(processDirectory) || !QDir().mkpath(QFileInfo(executablePath).path()))
        return false;
    QFile executable(executablePath);
    if (!executable.open(QIODevice::WriteOnly))
        return false;
    executable.write("synthetic process marker");
    executable.close();
    if (!QFile::link(executablePath, processDirectory + QStringLiteral("/exe")))
        return false;
    QFile commFile(processDirectory + QStringLiteral("/comm"));
    if (!commFile.open(QIODevice::WriteOnly))
        return false;
    commFile.write(comm.toLocal8Bit() + '\n');
    commFile.close();
    if (!commandLine.isEmpty()) {
        QFile commandLineFile(processDirectory + QStringLiteral("/cmdline"));
        if (!commandLineFile.open(QIODevice::WriteOnly))
            return false;
        commandLineFile.write(commandLine.toLocal8Bit());
        commandLineFile.close();
    }
    return true;
}
}

void GameMonitorTest::detectsConfiguredGameAndRestoresItsProfile()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString procRoot = temporary.filePath(QStringLiteral("proc"));
    const QString processDirectory = procRoot + QStringLiteral("/4242");
    const QString gameDirectory = temporary.filePath(QStringLiteral("steamapps/common/Example Arena/bin"));
    QVERIFY(QDir().mkpath(processDirectory));
    QVERIFY(QDir().mkpath(gameDirectory));

    const QString executablePath = gameDirectory + QStringLiteral("/arena-client");
    QFile executable(executablePath);
    QVERIFY(executable.open(QIODevice::WriteOnly));
    executable.write("synthetic executable marker");
    executable.close();
    QFile comm(processDirectory + QStringLiteral("/comm"));
    QVERIFY(comm.open(QIODevice::WriteOnly));
    comm.write("arena-client\n");
    comm.close();
    QVERIFY(QFile::link(executablePath, processDirectory + QStringLiteral("/exe")));

    const QString settingsPath = temporary.filePath(QStringLiteral("settings.ini"));
    GameMonitor monitor(settingsPath, procRoot);
    QCOMPARE(monitor.runningGameCandidates(), QStringList{QStringLiteral("arena-client")});
    QCOMPARE(monitor.presenceTargetName(), QStringLiteral("Example Arena"));
    QVERIFY(monitor.addProfile(QStringLiteral("arena-client"), QStringLiteral("Example Arena")));
    QCOMPARE(monitor.activeGameName(), QStringLiteral("Example Arena"));
    QCOMPARE(monitor.activeProcessName(), QStringLiteral("arena-client"));

    QSignalSpy profileSpy(&monitor, &GameMonitor::activeProfileChanged);
    monitor.setProfileAutoRecord(0, true);
    monitor.setProfileSettings(0, 60, 4, 90, 30);
    QVERIFY(profileSpy.count() >= 2);
    const QVariantMap activeProfile = monitor.activeProfileSettings();
    QVERIFY(activeProfile.value(QStringLiteral("autoRecord")).toBool());
    QCOMPARE(activeProfile.value(QStringLiteral("frameRate")).toInt(), 60);
    QCOMPARE(activeProfile.value(QStringLiteral("quality")).toInt(), 4);
    QCOMPARE(activeProfile.value(QStringLiteral("replaySeconds")).toInt(), 90);
    QCOMPARE(activeProfile.value(QStringLiteral("replayClipSeconds")).toInt(), 30);

    GameMonitor restored(settingsPath, procRoot);
    QCOMPARE(restored.profileCount(), 1);
    QCOMPARE(restored.activeGameName(), QStringLiteral("Example Arena"));
    QVERIFY(restored.activeProfileSettings().value(QStringLiteral("autoRecord")).toBool());

    QVERIFY(QFile::remove(processDirectory + QStringLiteral("/exe")));
    QVERIFY(QFile::remove(processDirectory + QStringLiteral("/comm")));
    monitor.refresh();
    QVERIFY(monitor.activeGameName().isEmpty());
    QVERIFY(monitor.activeProfileSettings().isEmpty());
    QVERIFY(monitor.presenceTargetName().isEmpty());
}

void GameMonitorTest::detectsLauncherWithoutNeedingAGameProfile_data()
{
    QTest::addColumn<QString>("processName");
    QTest::addColumn<QString>("executable");
    QTest::addColumn<QString>("commandLine");
    QTest::addColumn<QString>("expectedName");

    QTest::newRow("Steam") << QStringLiteral("steam")
        << QStringLiteral("home/player/.local/share/Steam/ubuntu12_32/steam")
        << QString() << QStringLiteral("Steam");
    QTest::newRow("Heroic") << QStringLiteral("heroic")
        << QStringLiteral("opt/heroic/heroic") << QString() << QStringLiteral("Heroic");
    QTest::newRow("Lutris") << QStringLiteral("python3")
        << QStringLiteral("usr/bin/python3") << QStringLiteral("python3 /usr/bin/lutris")
        << QStringLiteral("Lutris");
    QTest::newRow("GOG Galaxy") << QStringLiteral("GalaxyClient.exe")
        << QStringLiteral("gog/GalaxyClient.exe") << QString() << QStringLiteral("GOG Galaxy");
    QTest::newRow("Epic Games") << QStringLiteral("EpicGamesLauncher.exe")
        << QStringLiteral("epic/EpicGamesLauncher.exe") << QString() << QStringLiteral("Epic Games");
    QTest::newRow("itch.io") << QStringLiteral("itch")
        << QStringLiteral("opt/itch/itch") << QString() << QStringLiteral("itch.io");
    QTest::newRow("Battle.net") << QStringLiteral("Battle.net.exe")
        << QStringLiteral("battle-net/Battle.net.exe") << QString() << QStringLiteral("Battle.net");
    QTest::newRow("EA app") << QStringLiteral("EADesktop.exe")
        << QStringLiteral("ea/EADesktop.exe") << QString() << QStringLiteral("EA app");
    QTest::newRow("Ubisoft Connect") << QStringLiteral("upc.exe")
        << QStringLiteral("ubisoft/upc.exe") << QString() << QStringLiteral("Ubisoft Connect");
    QTest::newRow("Xbox") << QStringLiteral("GamingApp.exe")
        << QStringLiteral("xbox/GamingApp.exe") << QString() << QStringLiteral("Xbox");
}

void GameMonitorTest::detectsLauncherWithoutNeedingAGameProfile()
{
    QFETCH(QString, processName);
    QFETCH(QString, executable);
    QFETCH(QString, commandLine);
    QFETCH(QString, expectedName);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString procRoot = temporary.filePath(QStringLiteral("proc"));
    QVERIFY(createProcess(procRoot, QStringLiteral("4242"), temporary.filePath(executable),
                          processName, commandLine));
    GameMonitor monitor(temporary.filePath(QStringLiteral("settings.ini")), procRoot);
    QCOMPARE(monitor.presenceTargetName(), expectedName);
}

void GameMonitorTest::detectsInstalledGameAutomaticallyAndPrioritizesItOverLauncher()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString procRoot = temporary.filePath(QStringLiteral("proc"));
    QVERIFY(createProcess(procRoot, QStringLiteral("1000"),
                  temporary.filePath(QStringLiteral("home/player/.local/share/Steam/ubuntu12_32/steam")),
                  QStringLiteral("steam")));
    QVERIFY(createProcess(procRoot, QStringLiteral("2000"),
                  temporary.filePath(QStringLiteral("home/player/Games/Heroic/Sample Adventure/bin/game")),
                  QStringLiteral("sample-adventure")));

    GameMonitor monitor(temporary.filePath(QStringLiteral("settings.ini")), procRoot);
    QCOMPARE(monitor.presenceTargetName(), QStringLiteral("Sample Adventure"));
}

QTEST_MAIN(GameMonitorTest)
#include "game_monitor_test.moc"
