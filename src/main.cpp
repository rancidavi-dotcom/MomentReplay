#include "library/clip_library_model.h"
#include "library/clip_repository.h"
#include "recording/screen_recorder.h"
#include "recording/recording_preferences.h"
#include "recording/game_monitor.h"
#include "recording/global_shortcuts.h"
#include "desktop/tray_controller.h"
#include "desktop/capture_popup.h"
#include "desktop/discord_presence.h"
#include "editor/video_editor.h"
#include "share/google_drive_share.h"
#include "update/update_checker.h"
#include "friends/friends_client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIcon>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QDebug>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QSettings>
#include <QUrl>
#include <QWindow>
#include <QQuickWindow>

#ifndef MOMENT_FRIENDS_API_URL
#define MOMENT_FRIENDS_API_URL ""
#endif

#include <cstdio>
#ifdef Q_OS_LINUX
#include <sys/resource.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_LINUX
    // The replay encoder runs while a game is in the foreground. If Moment is
    // launched from a shell with elevated scheduling priority, FFmpeg's worker
    // threads inherit it and can preempt the game during clip export. Lower the
    // main thread before Qt creates capture/encoder workers so the game wins CPU
    // time under load. Raising the nice value is unprivileged and reversible.
    if (::setpriority(PRIO_PROCESS, 0, 10) != 0)
        std::fprintf(stderr, "Moment: could not lower background CPU priority.\n");
    QThreadPool::globalInstance()->setThreadPriority(QThread::LowPriority);
#endif
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
    QCoreApplication::setApplicationName(QStringLiteral("Moment"));
    QCoreApplication::setOrganizationName(QStringLiteral("Moment"));
#else
    QCoreApplication::setApplicationName(QStringLiteral("medal-linux"));
    QCoreApplication::setOrganizationName(QStringLiteral("MedalLinuxProject"));
#endif
    const QString flatpakAppId = qEnvironmentVariable("FLATPAK_ID");
    QGuiApplication::setDesktopFileName(flatpakAppId.isEmpty()
        ? QStringLiteral("io.moment.Moment") : flatpakAppId);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Moment"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MOMENT_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/io.moment.Moment.svg")));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption dataDirectoryOption(
        QStringList{QStringLiteral("data-dir")},
        QStringLiteral("Use an explicit data directory (useful for isolated runs)."),
        QStringLiteral("directory"));
    parser.addOption(dataDirectoryOption);
    const QCommandLineOption smokeTestOption(QStringLiteral("smoke-test"),
        QStringLiteral("Check UI startup with temporary data and no screen capture, then exit."));
    parser.addOption(smokeTestOption);
    const QCommandLineOption previewFriendsOption(QStringLiteral("preview-friends"),
        QStringLiteral("Render the Friends page in an isolated temporary window and save a PNG."),
        QStringLiteral("png"));
    parser.addOption(previewFriendsOption);
    const QCommandLineOption startHiddenOption(QStringLiteral("start-hidden"),
        QStringLiteral("Start capture and shortcuts without showing the main window."));
    parser.addOption(startHiddenOption);
    parser.process(app);

    QTemporaryDir smokeDirectory;
    const bool friendsPreview = parser.isSet(previewFriendsOption);
    const bool smokeTest = parser.isSet(smokeTestOption) || friendsPreview;
    if (smokeTest && !smokeDirectory.isValid())
        return 2;
    const QString dataDirectory = smokeTest ? smokeDirectory.path() : parser.isSet(dataDirectoryOption)
        ? QDir(parser.value(dataDirectoryOption)).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(dataDirectory))
        return 2;

    if (smokeTest) {
        QSettings settings(dataDirectory + QStringLiteral("/settings.ini"), QSettings::IniFormat);
        settings.setValue(QStringLiteral("discord/richPresenceEnabled"), false);
        settings.sync();
    }
    ClipRepository repository(dataDirectory + QStringLiteral("/library.sqlite3"));
    if (!repository.isOpen()) {
        qCritical("Unable to open the local clip library database: %s", qPrintable(repository.errorString()));
        return 2;
    }
    ClipLibraryModel library(repository);
    QString moviesDirectory = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (moviesDirectory.isEmpty())
        moviesDirectory = QDir(dataDirectory).filePath(QStringLiteral("Videos"));
    const QString videoDirectory = QDir(moviesDirectory).filePath(QStringLiteral("Moment"));
    QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (picturesDirectory.isEmpty())
        picturesDirectory = QDir(dataDirectory).filePath(QStringLiteral("Pictures"));
    const QString screenshotDirectory = QDir(picturesDirectory).filePath(QStringLiteral("Moment"));
    VideoEditor videoEditor(library, dataDirectory + QStringLiteral("/EditorProjects"), videoDirectory);
    GoogleDriveShare driveShare;
    RecordingPreferences preferences(dataDirectory + QStringLiteral("/settings.ini"));
    UpdateChecker updateChecker(dataDirectory + QStringLiteral("/settings.ini"),
                               QCoreApplication::applicationVersion(), !smokeTest);
    GameMonitor gameMonitor(dataDirectory + QStringLiteral("/settings.ini"));
    DiscordPresence discordPresence(gameMonitor, dataDirectory + QStringLiteral("/settings.ini"));
    ScreenRecorder recorder(library, preferences, videoDirectory, screenshotDirectory);
#ifdef Q_OS_WIN
    GlobalShortcuts globalShortcuts(dataDirectory + QStringLiteral("/settings.ini"));
#else
    GlobalShortcuts globalShortcuts;
#endif
    TrayController trayController;
    QQmlApplicationEngine engine;
    CapturePopup capturePopup(&engine);
    CapturePopup socialPopup(&engine, nullptr, QStringLiteral("social"), 112);
    FriendsClient friendsClient(dataDirectory + QStringLiteral("/settings.ini"),
                                QString::fromUtf8(MOMENT_FRIENDS_API_URL), nullptr, !smokeTest);
    if (friendsPreview)
        friendsClient.preparePreviewData();
    QObject::connect(&friendsClient, &FriendsClient::incomingNotification, &app,
                     [&socialPopup](const QString &title, const QString &message, bool warning) {
        socialPopup.show(title, message, warning);
    });
    QObject::connect(&friendsClient, &FriendsClient::clipDownloaded, &app,
                     [&library, &socialPopup](const QString &path) {
        library.importFile(path);
        socialPopup.show(QStringLiteral("Video received"), QFileInfo(path).fileName());
    });
    bool recordingStartedByGame = false;
    bool activeProfileRequestsRecording = false;
    QString recordingGameProcess;
    const auto applyActiveGameProfile = [&] {
        const QVariantMap profile = gameMonitor.activeProfileSettings();
        preferences.setActiveGameProfile(profile);
        activeProfileRequestsRecording = profile.value(QStringLiteral("autoRecord")).toBool();
        const QString requestedProcess = gameMonitor.activeProcessName();
        if (recordingStartedByGame
            && (!activeProfileRequestsRecording || recordingGameProcess != requestedProcess)) {
            if (recorder.recording())
                recorder.stopRecording();
            recordingStartedByGame = false;
            recordingGameProcess.clear();
        }
        if (activeProfileRequestsRecording && !recordingStartedByGame
            && !recorder.recording() && recorder.buffering()) {
            recordingStartedByGame = true;
            recordingGameProcess = requestedProcess;
            recorder.startRecording();
        }
    };
    QObject::connect(&gameMonitor, &GameMonitor::activeProfileChanged, &app,
                     [applyActiveGameProfile](const QVariantMap &) { applyActiveGameProfile(); });
    QObject::connect(&recorder, &ScreenRecorder::stateChanged, &app, applyActiveGameProfile);
    QObject::connect(&globalShortcuts, &GlobalShortcuts::saveReplayRequested,
                     &recorder, [&recorder, &preferences, &capturePopup] {
        qInfo() << "Moment: replay shortcut dispatched to recorder";
        capturePopup.show(QStringLiteral("Saving replay…"),
                          QStringLiteral("Preparing your recent gameplay clip."), false, {}, true);
        recorder.saveReplayClip(preferences.effectiveReplayClipSeconds());
        if (recorder.statusMessage().startsWith(QStringLiteral("Replay save queued")))
            capturePopup.show(QStringLiteral("Replay queued"), recorder.statusMessage(), false, {}, true);
    });
    QObject::connect(&globalShortcuts, &GlobalShortcuts::saveScreenshotRequested,
                     &recorder, [&recorder, &capturePopup] {
        capturePopup.show(QStringLiteral("Saving screenshot…"),
                          QStringLiteral("Saving the current screen frame."), false, {}, true);
        recorder.saveScreenshot();
        if (recorder.statusMessage().startsWith(QStringLiteral("Screenshot queued")))
            capturePopup.show(QStringLiteral("Screenshot queued"), recorder.statusMessage(), false, {}, true);
    });
    QObject::connect(&recorder, &ScreenRecorder::replayClipSaved, &capturePopup,
                     [&capturePopup](const QString &path) {
        capturePopup.show(QStringLiteral("Replay saved"), QFileInfo(path).fileName());
    });
    QObject::connect(&recorder, &ScreenRecorder::recordingSaved, &capturePopup,
                     [&capturePopup](const QString &path) {
        capturePopup.show(QStringLiteral("Recording saved"), QFileInfo(path).fileName());
    });
    QObject::connect(&recorder, &ScreenRecorder::screenshotSaved, &capturePopup,
                     [&capturePopup](const QString &path) {
        capturePopup.show(QStringLiteral("Screenshot saved"), QFileInfo(path).fileName(),
                          false, QUrl::fromLocalFile(path));
    });
    QObject::connect(&recorder, &ScreenRecorder::operationFailed, &capturePopup,
                     [&capturePopup](const QString &message) {
        capturePopup.show(QStringLiteral("Capture could not finish"), message, true);
    });
    QObject::connect(&videoEditor, &VideoEditor::exportSucceeded, &capturePopup,
                     [&capturePopup](const QString &path) {
        capturePopup.show(QStringLiteral("Edited clip exported"), QFileInfo(path).fileName());
    });
    QObject::connect(&driveShare, &GoogleDriveShare::shareCompleted, &capturePopup,
                     [&capturePopup](const QString &) {
        capturePopup.show(QStringLiteral("Share link ready"),
                          QStringLiteral("Copied to clipboard. Paste it anywhere to share this clip."));
    });
    QObject::connect(&driveShare, &GoogleDriveShare::shareFailed, &capturePopup,
                     [&capturePopup](const QString &message) {
        capturePopup.show(QStringLiteral("Clip sharing failed"), message, true);
    });

    QTimer shutdownPoll;
    shutdownPoll.setInterval(100);
    QObject::connect(&trayController, &TrayController::quitRequested, &app, [&] {
        if (recorder.recording())
            recorder.stopRecording();
        shutdownPoll.start();
    });
    QObject::connect(&shutdownPoll, &QTimer::timeout, &app, [&] {
        if (recorder.recording() || recorder.replayExporting())
            return;
        if (recorder.busy()) {
            recorder.stopCapture();
            if (recorder.busy())
                return;
        }
        shutdownPoll.stop();
        app.quit();
    });

    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const auto &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("clipLibrary"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("screenRecorder"), &recorder);
    engine.rootContext()->setContextProperty(QStringLiteral("recordingPreferences"), &preferences);
    engine.rootContext()->setContextProperty(QStringLiteral("updateChecker"), &updateChecker);
    engine.rootContext()->setContextProperty(QStringLiteral("gameMonitor"), &gameMonitor);
    engine.rootContext()->setContextProperty(QStringLiteral("discordPresence"), &discordPresence);
    engine.rootContext()->setContextProperty(QStringLiteral("videoEditor"), &videoEditor);
    engine.rootContext()->setContextProperty(QStringLiteral("driveShare"), &driveShare);
    engine.rootContext()->setContextProperty(QStringLiteral("globalShortcuts"), &globalShortcuts);
    engine.rootContext()->setContextProperty(QStringLiteral("trayController"), &trayController);
    engine.rootContext()->setContextProperty(QStringLiteral("friendsClient"), &friendsClient);
    const QUrl mainQml(QStringLiteral("qrc:/qt/qml/MedalLinux/qml/Main.qml"));
    if (!QFile::exists(mainQml.toString().mid(3)))
        return 4;
    QQmlComponent component(&engine, mainQml);
    if (component.isError()) {
        for (const auto &error : component.errors())
            std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
        if (component.errors().isEmpty())
            std::fprintf(stderr, "QML component entered error state without diagnostics.\n");
        return 3;
    }
    QObject *root = component.createWithInitialProperties({
        {QStringLiteral("autoCaptureEnabled"), !smokeTest},
        {QStringLiteral("startHidden"), parser.isSet(startHiddenOption)},
        {QStringLiteral("page"), friendsPreview ? 4 : 0}});
    if (!root) {
        for (const auto &error : component.errors())
            std::fprintf(stderr, "%s\n", qPrintable(error.toString()));
        if (component.errors().isEmpty())
            std::fprintf(stderr, "QML component creation returned null without diagnostics.\n");
        return 5;
    }
    root->setParent(&engine);
    auto *mainWindow = qobject_cast<QWindow *>(root);
    trayController.setMainWindow(mainWindow);
    if (friendsPreview) {
        const QString previewPath = QFileInfo(parser.value(previewFriendsOption)).absoluteFilePath();
        QTimer::singleShot(700, &app, [mainWindow, previewPath, &app] {
            auto *quickWindow = qobject_cast<QQuickWindow *>(mainWindow);
            const QImage image = quickWindow ? quickWindow->grabWindow() : QImage();
            if (image.isNull() || !image.save(previewPath)) {
                qCritical("Could not save isolated Friends page preview to %s", qPrintable(previewPath));
                app.exit(6);
                return;
            }
            qInfo("Saved isolated Friends page preview to %s", qPrintable(previewPath));
            app.quit();
        });
    } else if (smokeTest)
        QTimer::singleShot(1000, &app, &QCoreApplication::quit);
    else
        trayController.showTrayIcon();
    return app.exec();
}
