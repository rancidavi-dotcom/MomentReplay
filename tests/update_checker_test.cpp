#include "update/update_checker.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class UpdateCheckerTest final : public QObject {
    Q_OBJECT
private slots:
    void installsVerifiedPackageFromGitHubRelease()
    {
        const QUrl apiUrl(qEnvironmentVariable("MOMENT_UPDATE_TEST_API_URL"));
        if (!apiUrl.isValid() || apiUrl.isEmpty())
            QSKIP("Set MOMENT_UPDATE_TEST_API_URL to a temporary GitHub pre-release API URL to run the live update test.");

        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString settingsFile = temporary.filePath(QStringLiteral("settings.ini"));
        QSettings settings(settingsFile, QSettings::IniFormat);
        settings.setValue(QStringLiteral("updates/autoCheck"), false);
        settings.sync();

        const QString target = temporary.filePath(QStringLiteral("home/.local/bin/medal-linux"));
        QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
        QFile installed(target);
        QVERIFY(installed.open(QIODevice::WriteOnly));
        QCOMPARE(installed.write("old test package"), qint64(16));
        installed.close();

        UpdateChecker checker(settingsFile, QStringLiteral("0.0.1"), true, nullptr, apiUrl, target);
        QSignalSpy stateChanges(&checker, &UpdateChecker::stateChanged);
        checker.checkNow();
        QTRY_VERIFY_WITH_TIMEOUT(!checker.checking(), 30000);
        QVERIFY(checker.installSupported());
        QVERIFY2(checker.updateAvailable(), qPrintable(checker.status()));
        QVERIFY2(checker.canInstallUpdate(), qPrintable(checker.status()));

        checker.downloadAndInstall();
        QTRY_VERIFY_WITH_TIMEOUT(!checker.downloading(), 30000);
        QVERIFY2(checker.restartRequired(), qPrintable(checker.status()));
        QCOMPARE(checker.status(), QStringLiteral("Update installed. Restart Moment to use it."));
        QCOMPARE(stateChanges.count() > 0, true);

        QVERIFY(installed.open(QIODevice::ReadOnly));
        QCOMPARE(installed.readAll(), QByteArray("MOMENT-UPDATER-TEST-PACKAGE-v0.0.2\n"));
    }
};

QTEST_GUILESS_MAIN(UpdateCheckerTest)
#include "update_checker_test.moc"
