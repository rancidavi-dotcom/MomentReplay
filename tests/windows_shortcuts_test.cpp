#include "recording/global_shortcuts.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <windows.h>

class WindowsShortcutsTest final : public QObject {
    Q_OBJECT
private slots:
    void savesRestoresAndReleasesNativeRegistrations()
    {
        QTemporaryDir temporary;
        const QString settings = temporary.filePath("settings.ini");
        {
            GlobalShortcuts shortcuts(settings);
            QVERIFY(shortcuts.setCaptureShortcuts("Ctrl+Shift+F10", "Ctrl+Shift+F11"));
            QVERIFY(shortcuts.allRegistered());
            QCOMPARE(shortcuts.triggerDescription(), QString("Ctrl+Shift+F10"));
            QVERIFY(!shortcuts.setCaptureShortcuts("F12", "F9"));
            QVERIFY(!shortcuts.setCaptureShortcuts("F8", "F8"));
            QVERIFY(shortcuts.allRegistered());
            QCOMPARE(shortcuts.triggerDescription(), QString("Ctrl+Shift+F10"));
            // A second object cannot steal OS registrations from the first.
            GlobalShortcuts conflict(temporary.filePath("other.ini"));
            QVERIFY(!conflict.setCaptureShortcuts("Ctrl+Shift+F10", "Ctrl+Shift+F11"));
            QVERIFY(shortcuts.allRegistered());
        }
        GlobalShortcuts restored(settings);
        restored.requestCaptureShortcuts();
        QVERIFY(restored.allRegistered());
        QCOMPARE(restored.triggerDescription(), QString("Ctrl+Shift+F10"));
        QCOMPARE(restored.screenshotTriggerDescription(), QString("Ctrl+Shift+F11"));
        QSettings stored(settings, QSettings::IniFormat);
        QCOMPARE(stored.value("shortcuts/windowsReplay").toString(), QString("Ctrl+Shift+F10"));
    }

    void receivesGlobalKeysWithoutAMomentWindow()
    {
        QTemporaryDir temporary;
        GlobalShortcuts shortcuts(temporary.filePath("settings.ini"));
        QVERIFY(shortcuts.setCaptureShortcuts("Ctrl+Shift+F10", "Ctrl+Shift+F11"));
        // Drain the constructor's deferred registration before generating input.
        QCoreApplication::processEvents();
        QSignalSpy replay(&shortcuts, &GlobalShortcuts::saveReplayRequested);
        QSignalSpy screenshot(&shortcuts, &GlobalShortcuts::saveScreenshotRequested);
        auto sendChord = [](WORD key) {
            INPUT inputs[6]{};
            const WORD keys[] = {VK_CONTROL, VK_SHIFT, key, key, VK_SHIFT, VK_CONTROL};
            for (int i = 0; i < 6; ++i) {
                inputs[i].type = INPUT_KEYBOARD;
                inputs[i].ki.wVk = keys[i];
                inputs[i].ki.dwFlags = i >= 3 ? KEYEVENTF_KEYUP : 0;
            }
            return SendInput(6, inputs, sizeof(INPUT));
        };
        QCOMPARE(sendChord(VK_F10), UINT(6));
        QTRY_COMPARE_WITH_TIMEOUT(replay.count(), 1, 3000);
        QCOMPARE(screenshot.count(), 0);
        QCOMPARE(sendChord(VK_F11), UINT(6));
        QTRY_COMPARE_WITH_TIMEOUT(screenshot.count(), 1, 3000);
        QCOMPARE(replay.count(), 1);
    }
};

QTEST_MAIN(WindowsShortcutsTest)
#include "windows_shortcuts_test.moc"
