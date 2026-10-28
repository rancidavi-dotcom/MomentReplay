#include "share/google_drive_share.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class GoogleDriveShareTest final : public QObject {
    Q_OBJECT

private slots:
    void refusesToUploadUntilOAuthClientIsConfigured()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString clipPath = temporary.filePath(QStringLiteral("sample.mp4"));
        QFile clip(clipPath);
        QVERIFY(clip.open(QIODevice::WriteOnly));
        QCOMPARE(clip.write("synthetic clip"), qint64(14));
        clip.close();

        GoogleDriveShare sharing(QStringLiteral("MomentShareTest-")
                                     + QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(!sharing.configured());
        QSignalSpy failed(&sharing, &GoogleDriveShare::shareFailed);
        sharing.shareClip(clipPath);

        QCOMPARE(failed.count(), 1);
        QVERIFY(!sharing.busy());
        QVERIFY(sharing.statusMessage().contains(QStringLiteral("not enabled")));
    }
};

QTEST_GUILESS_MAIN(GoogleDriveShareTest)
#include "google_drive_share_test.moc"
