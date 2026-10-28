#include "desktop/capture_popup.h"

#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>

class CapturePopupTest : public QObject {
    Q_OBJECT
private slots:
    void popupUsesOwnWindowWithoutInputOrFocus()
    {
        QQmlEngine engine;
        CapturePopup popup(&engine);
        QCOMPARE(popup.window()->status(), QQuickView::Ready);
        QVERIFY(!popup.window()->isVisible());
        popup.show(QStringLiteral("Replay saved"), QStringLiteral("Ready in your Library"));
        QVERIFY(popup.window()->isVisible());
        QVERIFY(popup.window()->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QVERIFY(popup.window()->flags().testFlag(Qt::WindowTransparentForInput));
        QCOMPARE(popup.window()->rootObject()->property("heading").toString(), QStringLiteral("Replay saved"));
        QTest::qWait(250);
        const QString previewPath = qEnvironmentVariable("MOMENT_POPUP_PREVIEW");
        if (!previewPath.isEmpty())
            QVERIFY(popup.window()->grabWindow().save(previewPath));
        popup.show(QStringLiteral("Replay is not ready"), QStringLiteral("Capture is waiting for frames"), true);
        QVERIFY(popup.window()->rootObject()->property("isWarning").toBool());
        QCOMPARE(popup.window()->rootObject()->property("sequence").toInt(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!popup.window()->isVisible(), 5000);
    }
};

QTEST_MAIN(CapturePopupTest)
#include "capture_popup_test.moc"
