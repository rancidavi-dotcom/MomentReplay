#include "recording/global_shortcuts.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QWindow>
#include <QtTest>

#include "fake_shortcuts_portal.h"

class GlobalShortcutsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qDBusRegisterMetaType<PortalShortcut>();
        qDBusRegisterMetaType<QList<PortalShortcut>>();
        QVERIFY(m_server.isConnected());
        QVERIFY(m_server.registerService(QStringLiteral("org.freedesktop.portal.Desktop")));
        QVERIFY(m_server.registerVirtualObject(QStringLiteral("/org/freedesktop/portal/desktop"),
                                               &m_portal, QDBusConnection::SubPath));
    }

    void init()
    {
        // Drain asynchronous Session.Close from the previous test's destructor.
        QCoreApplication::processEvents();
        m_portal.saved.clear();
        m_portal.boundIds.clear();
        m_portal.listCalls = 0;
        m_portal.bindCalls = 0;
        m_portal.createCalls = 0;
        m_portal.closeCalls = 0;
        m_portal.bindResponse = 0;
        m_portal.boundSessions.clear();
        m_portal.acceptBindings = true;
    }

    void restoredActionsMustBeBoundBeforeActivation()
    {
        m_portal.saved = {{QStringLiteral("save-replay"), {{"trigger_description", "Ctrl+F8"}}},
                          {QStringLiteral("save-screenshot"), {{"trigger_description", "Shift+F9"}}}};
        GlobalShortcuts shortcuts;
        QSignalSpy replay(&shortcuts, &GlobalShortcuts::saveReplayRequested);
        QSignalSpy screenshot(&shortcuts, &GlobalShortcuts::saveScreenshotRequested);
        QTRY_VERIFY(shortcuts.allRegistered());
        QCOMPARE(m_portal.bindCalls, 1);
        QCOMPARE(m_portal.boundIds, QStringList({"save-replay", "save-screenshot"}));
        QCOMPARE(shortcuts.triggerDescription(), QStringLiteral("Ctrl+F8"));
        QCOMPARE(shortcuts.screenshotTriggerDescription(), QStringLiteral("Shift+F9"));
        m_portal.activate(m_server, QStringLiteral("save-replay"));
        m_portal.activate(m_server, QStringLiteral("save-screenshot"));
        QTRY_COMPARE(replay.count(), 1);
        QTRY_COMPARE(screenshot.count(), 1);
    }

    void newInstallAutomaticallyBindsBothGlobalActions()
    {
        GlobalShortcuts shortcuts;
        QTRY_VERIFY(shortcuts.allRegistered());
        QCOMPARE(m_portal.bindCalls, 1);
        QCOMPARE(m_portal.boundIds, QStringList({"save-replay", "save-screenshot"}));
        QCOMPARE(shortcuts.triggerDescription(), QStringLiteral("F8"));
        QCOMPARE(shortcuts.screenshotTriggerDescription(), QStringLiteral("F9"));
    }

    void unassignedKeysAreNotReportedActive()
    {
        m_portal.saved = {{QStringLiteral("save-replay"), {{"trigger_description", ""}}}};
        m_portal.acceptBindings = false;
        GlobalShortcuts shortcuts;
        QTRY_COMPARE(m_portal.bindCalls, 1);
        QTRY_VERIFY(!shortcuts.requesting());
        QVERIFY(!shortcuts.registered());
        QVERIFY(!shortcuts.screenshotRegistered());
    }

    void activatesWhileWindowHiddenAndAnotherWindowHasFocus()
    {
        QWindow moment;
        moment.show();
        QWindow other;
        other.show();
        other.requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), &other);
        GlobalShortcuts shortcuts;
        QSignalSpy replay(&shortcuts, &GlobalShortcuts::saveReplayRequested);
        QSignalSpy screenshot(&shortcuts, &GlobalShortcuts::saveScreenshotRequested);
        QTRY_VERIFY(shortcuts.allRegistered());
        moment.hide();
        QVERIFY(!moment.isVisible());
        m_portal.activate(m_server, QStringLiteral("save-replay"));
        m_portal.activate(m_server, QStringLiteral("save-screenshot"));
        QTRY_COMPARE(replay.count(), 1);
        QTRY_COMPARE(screenshot.count(), 1);
        QCOMPARE(QGuiApplication::focusWindow(), &other);
        QVERIFY(!moment.isVisible());
    }

    void cancelledSetupCanRetryWithANewSession()
    {
        m_portal.bindResponse = 1;
        GlobalShortcuts shortcuts;
        QTRY_COMPARE(m_portal.bindCalls, 1);
        QTRY_VERIFY(!shortcuts.requesting());
        QVERIFY(!shortcuts.allRegistered());
        const QString previousSession = m_portal.sessionPath;
        m_portal.bindResponse = 0;
        shortcuts.requestCaptureShortcuts();
        QTRY_VERIFY(shortcuts.allRegistered());
        QCOMPARE(m_portal.bindCalls, 2);
        QCOMPARE(m_portal.createCalls, 2);
        QVERIFY(previousSession != m_portal.sessionPath);
    }

    void desktopChangesUpdateOnlyTheChangedAction()
    {
        GlobalShortcuts shortcuts;
        QTRY_VERIFY(shortcuts.allRegistered());
        m_portal.change(m_server, {{QStringLiteral("save-screenshot"), {{"trigger_description", "Ctrl+F9"}}}});
        QTRY_COMPARE(shortcuts.screenshotTriggerDescription(), QStringLiteral("Ctrl+F9"));
        QVERIFY(shortcuts.registered());
        m_portal.change(m_server, {{QStringLiteral("save-replay"), {{"trigger_description", ""}}}});
        QTRY_VERIFY(!shortcuts.registered());
        QVERIFY(shortcuts.screenshotRegistered());
    }

    void portalRestartRebindsWithoutOpeningTheApp()
    {
        GlobalShortcuts shortcuts;
        QTRY_VERIFY(shortcuts.allRegistered());
        QVERIFY(m_server.unregisterService(QStringLiteral("org.freedesktop.portal.Desktop")));
        QTRY_VERIFY(!shortcuts.portalAvailable());
        QVERIFY(!shortcuts.allRegistered());
        QVERIFY(m_server.registerService(QStringLiteral("org.freedesktop.portal.Desktop")));
        QTRY_VERIFY(shortcuts.allRegistered());
        QCOMPARE(m_portal.bindCalls, 2);
        QSignalSpy replay(&shortcuts, &GlobalShortcuts::saveReplayRequested);
        m_portal.activate(m_server, QStringLiteral("save-replay"));
        QTRY_COMPARE(replay.count(), 1);
    }

    void closedSessionDoesNotPretendKeysAreActive()
    {
        GlobalShortcuts shortcuts;
        QTRY_VERIFY(shortcuts.allRegistered());
        auto closed = QDBusMessage::createSignal(m_portal.sessionPath,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Closed"));
        m_server.send(closed);
        QTRY_VERIFY(!shortcuts.allRegistered());
        QVERIFY(!shortcuts.requesting());
        shortcuts.requestCaptureShortcuts();
        QTRY_VERIFY(shortcuts.allRegistered());
        QCOMPARE(m_portal.bindCalls, 2);
    }

private:
    QDBusConnection m_server = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                            QStringLiteral("moment_fake_portal"));
    FakeShortcutsPortal m_portal;
};

QTEST_MAIN(GlobalShortcutsTest)
#include "global_shortcuts_test.moc"
