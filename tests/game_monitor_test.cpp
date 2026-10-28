#include "../src/recording/game_monitor.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class GameMonitorTest final : public QObject {
    Q_OBJECT

private slots:
    void detectsConfiguredGameAndRestoresItsProfile();
};

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
}

QTEST_MAIN(GameMonitorTest)
#include "game_monitor_test.moc"
