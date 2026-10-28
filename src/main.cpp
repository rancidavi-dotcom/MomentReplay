#include "library/clip_library_model.h"
#include "library/clip_repository.h"
#include "recording/screen_recorder.h"
#include "recording/recording_preferences.h"
#include "recording/game_monitor.h"
#include "recording/global_shortcuts.h"
#include "desktop/tray_controller.h"
#include "desktop/capture_popup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <QUrl>
#include <QWindow>

#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName(QStringLiteral("medal-linux"));
    QCoreApplication::setOrganizationName(QStringLiteral("MedalLinuxProject"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.moment.Moment"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Moment"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/io.moment.Moment.svg")));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption dataDirectoryOption(
        QStringList{QStringLiteral("data-dir")},
        QStringLiteral("Use an explicit data directory (useful for isolated runs)."),
        QStringLiteral("directory"));
    parser.addOption(dataDirectoryOption);
    parser.process(app);

    const QString dataDirectory = parser.isSet(dataDirectoryOption)
        ? QDir(parser.value(dataDirectoryOption)).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(dataDirectory))
        return 2;

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
    RecordingPreferences preferences(dataDirectory + QStringLiteral("/settings.ini"));
    GameMonitor gameMonitor(dataDirectory + QStringLiteral("/settings.ini"));
    ScreenRecorder recorder(library, preferences, videoDirectory, screenshotDirectory);
    GlobalShortcuts globalShortcuts;
    TrayController trayController;
    QQmlApplicationEngine engine;
    CapturePopup capturePopup(&engine);
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
                     &recorder, [&recorder, &globalShortcuts, &preferences, &capturePopup] {
        qInfo() << "Moment: replay shortcut dispatched to recorder";
        if (!recorder.buffering() || recorder.bufferedSeconds() == 0) {
            globalShortcuts.reportReplayUnavailable();
            capturePopup.show(QStringLiteral("Replay is not ready"),
                              QStringLiteral("Let capture run for a few seconds and try again."), true);
            return;
        }
        recorder.saveReplayClip(preferences.effectiveReplayClipSeconds());
    });
    QObject::connect(&globalShortcuts, &GlobalShortcuts::saveScreenshotRequested,
                     &recorder, &ScreenRecorder::saveScreenshot);
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
    engine.rootContext()->setContextProperty(QStringLiteral("gameMonitor"), &gameMonitor);
    engine.rootContext()->setContextProperty(QStringLiteral("globalShortcuts"), &globalShortcuts);
    engine.rootContext()->setContextProperty(QStringLiteral("trayController"), &trayController);
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
    QObject *root = component.create();
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
    trayController.showTrayIcon();
    return app.exec();
}
