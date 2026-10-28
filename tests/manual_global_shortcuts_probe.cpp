// Manual KDE probe: registers only Moment shortcuts; no screen capture or physical key injection.
// Not part of CTest: uses the current desktop session and its existing Moment bindings.
#include "recording/global_shortcuts.h"
#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusMessage>
#include <QDebug>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setDesktopFileName("io.moment.Moment");
    auto bus = QDBusConnection::sessionBus();
    auto registry = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.host.portal.Registry", "Register");
    registry << QString("io.moment.Moment") << QVariantMap{};
    const auto registration = bus.call(registry);
    if (registration.type() == QDBusMessage::ErrorMessage) {
        qCritical() << registration.errorMessage();
        return 4;
    }
    GlobalShortcuts shortcuts;
    bool invoked = false, replay = false, screenshot = false;
    QObject::connect(&shortcuts, &GlobalShortcuts::saveReplayRequested, &app, [&] {
        replay = true; qInfo() << "LIVE PORTAL: replay activation received";
        if (screenshot) app.quit();
    });
    QObject::connect(&shortcuts, &GlobalShortcuts::saveScreenshotRequested, &app, [&] {
        screenshot = true; qInfo() << "LIVE PORTAL: screenshot activation received";
        if (replay) app.quit();
    });
    QObject::connect(&shortcuts, &GlobalShortcuts::stateChanged, &app, [&] {
        if (shortcuts.requesting() || invoked) return;
        qInfo().noquote() << shortcuts.statusMessage();
        if (!shortcuts.allRegistered()) { app.exit(2); return; }
        invoked = true;
        for (const QString &id : {QString("save-replay"), QString("save-screenshot")}) {
            auto trigger = QDBusMessage::createMethodCall("org.kde.kglobalaccel", "/component/io_moment_Moment", "org.kde.kglobalaccel.Component", "invokeShortcut");
            trigger << id;
            bus.asyncCall(trigger);
        }
    });
    QTimer::singleShot(10000, &app, [&] { qCritical() << "LIVE PORTAL timeout"; app.exit(3); });
    const int result = app.exec();
    std::printf("LIVE KDE PORTAL: replay=%d screenshot=%d key=%s screenshotKey=%s result=%d\n", replay, screenshot, qPrintable(shortcuts.triggerDescription()), qPrintable(shortcuts.screenshotTriggerDescription()), result);
    return result == 0 && replay && screenshot ? 0 : result == 0 ? 5 : result;
}
