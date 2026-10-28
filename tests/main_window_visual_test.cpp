#include "library/clip_library_model.h"
#include "library/clip_repository.h"
#include "recording/screen_recorder.h"
#include "recording/recording_preferences.h"
#include "recording/game_monitor.h"
#include "editor/video_editor.h"

#include <QDir>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class VisualShortcuts final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool portalAvailable MEMBER enabled CONSTANT)
    Q_PROPERTY(bool registered MEMBER enabled CONSTANT)
    Q_PROPERTY(bool screenshotRegistered MEMBER enabled CONSTANT)
    Q_PROPERTY(bool allRegistered MEMBER enabled CONSTANT)
    Q_PROPERTY(bool requesting MEMBER requesting CONSTANT)
    Q_PROPERTY(QString triggerDescription MEMBER replayKey CONSTANT)
    Q_PROPERTY(QString screenshotTriggerDescription MEMBER screenshotKey CONSTANT)
    Q_PROPERTY(QString statusMessage MEMBER status CONSTANT)
public:
    bool enabled = true;
    bool requesting = false;
    QString replayKey = QStringLiteral("F8");
    QString screenshotKey = QStringLiteral("F9");
    QString status = QStringLiteral("Global capture shortcuts active · replay F8 · screenshot F9");
    Q_INVOKABLE void requestCaptureShortcuts() {}
};

class VisualTray final : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE bool hideWindowToTray() { return true; }
};

class VisualDiscordPresence final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled CONSTANT)
    Q_PROPERTY(QString status READ status CONSTANT)
    Q_PROPERTY(QString activeGameName READ activeGameName NOTIFY activeGameNameChanged)
    Q_PROPERTY(bool connected READ connected CONSTANT)
public:
    bool enabled() const { return true; }
    QString status() const { return QStringLiteral("Demo preview · Discord is not connected"); }
    QString activeGameName() const { return m_activeGameName; }
    bool connected() const { return false; }
    Q_INVOKABLE void setEnabled(bool) {}
    void setDemoActiveGameName(const QString &name)
    {
        if (m_activeGameName == name)
            return;
        m_activeGameName = name;
        emit activeGameNameChanged();
    }

signals:
    void activeGameNameChanged();

private:
    QString m_activeGameName = QStringLiteral("Sample Game");
};

class VisualDriveShare final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool connected READ connected CONSTANT)
    Q_PROPERTY(bool busy READ busy CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage CONSTANT)
    Q_PROPERTY(QString lastShareUrl READ lastShareUrl CONSTANT)
public:
    bool configured() const { return true; }
    bool connected() const { return true; }
    bool busy() const { return false; }
    QString statusMessage() const { return QStringLiteral("Demo preview · share link copied"); }
    QString lastShareUrl() const { return QStringLiteral("https://drive.google.com/file/d/demo-clip/view"); }
    Q_INVOKABLE void shareClip(const QString &) {}
    Q_INVOKABLE void shareClipUrl(const QUrl &) {}
    Q_INVOKABLE void copyLastShareUrl() {}
};

class MainWindowVisualTest final : public QObject {
    Q_OBJECT
private:
    static QImage sampleFrame(int variant)
    {
        QImage image(960, 540, QImage::Format_ARGB32);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor skies[] = {QColor("#15364a"), QColor("#5d374c"), QColor("#16453c")};
        QLinearGradient sky(0, 0, 0, 540);
        sky.setColorAt(0, QColor("#0c1726"));
        sky.setColorAt(1, skies[variant]);
        painter.fillRect(image.rect(), sky);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#b5ec72"));
        painter.drawEllipse(QPointF(710, 150), 44, 44);
        for (int layer = 0; layer < 3; ++layer) {
            QPolygonF mountain;
            mountain << QPointF(0, 540) << QPointF(0, 315 + layer * 48);
            for (int i = 0; i <= 8; ++i)
                mountain << QPointF(i * 140, 250 + layer * 70 + ((i * 47 + variant * 33) % 110));
            mountain << QPointF(960, 540);
            painter.setBrush(layer == 0 ? QColor("#28434d") : layer == 1 ? QColor("#172d36") : QColor("#0c1c26"));
            painter.drawPolygon(mountain);
        }
        painter.setPen(QColor("#afbfcb"));
        painter.setFont(QFont(QStringLiteral("sans-serif"), 13));
        painter.drawText(QRect(36, 470, 880, 40), QStringLiteral("MOMENT  /  GENERATED DEMO FRAME"));
        return image;
    }

private slots:
    void initTestCase() { QQuickStyle::setStyle(QStringLiteral("Fusion")); }

    void renderActualPagesWithIsolatedDemoMedia()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        ClipRepository repository(temporary.filePath("library.sqlite3"));
        QVERIFY(repository.isOpen());
        ClipLibraryModel library(repository);
        for (int i = 0; i < 3; ++i) {
            const QString names[] = {"Sample - Blue hour.png", "Sample - Last light.png", "Sample - Green horizon.png"};
            const QString path = temporary.filePath(names[i]);
            QVERIFY(sampleFrame(i).save(path));
            library.importFile(path);
        }
        QCOMPARE(library.clipCount(), 3);
        const QString demoVideo = temporary.filePath(QStringLiteral("Sample - Editor clip.mp4"));
        QProcess videoGenerator;
        videoGenerator.start(QStandardPaths::findExecutable(QStringLiteral("ffmpeg")), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("testsrc2=size=960x540:rate=30:duration=8"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("sine=frequency=440:duration=8"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:a"), QStringLiteral("aac"),
            QStringLiteral("-shortest"), demoVideo
        });
        QVERIFY(videoGenerator.waitForStarted(3000));
        QVERIFY(videoGenerator.waitForFinished(15000));
        QCOMPARE(videoGenerator.exitStatus(), QProcess::NormalExit);
        QCOMPARE(videoGenerator.exitCode(), 0);
        QVERIFY(QFileInfo::exists(demoVideo));
        library.importFile(demoVideo);
        QCOMPARE(library.clipCount(), 4);
        RecordingPreferences preferences(temporary.filePath("settings.ini"));
        GameMonitor gameMonitor(temporary.filePath("settings.ini"));
        QVERIFY(gameMonitor.addProfile(QStringLiteral("demo-game-bin"), QStringLiteral("Demo Game (sample)")));
        QVideoFrameInput input(QVideoFrameFormat(QSize(960, 540), QVideoFrameFormat::Format_ARGB8888));
        ScreenRecorder recorder(library, preferences, temporary.filePath("videos"), temporary.filePath("pictures"), input);
        VideoEditor videoEditor(library, temporary.filePath("EditorProjects"), temporary.filePath("exports"));
        VisualShortcuts shortcuts;
        VisualTray tray;
        VisualDiscordPresence discordPresence;
        VisualDriveShare driveShare;
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError> &errors) {
            for (const auto &error : errors)
                warnings.append(error.toString());
        });
        engine.rootContext()->setContextProperty("clipLibrary", &library);
        engine.rootContext()->setContextProperty("screenRecorder", &recorder);
        engine.rootContext()->setContextProperty("recordingPreferences", &preferences);
        engine.rootContext()->setContextProperty("gameMonitor", &gameMonitor);
        engine.rootContext()->setContextProperty("discordPresence", &discordPresence);
        engine.rootContext()->setContextProperty("videoEditor", &videoEditor);
        engine.rootContext()->setContextProperty("driveShare", &driveShare);
        engine.rootContext()->setContextProperty("globalShortcuts", &shortcuts);
        engine.rootContext()->setContextProperty("trayController", &tray);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/MedalLinux/qml/Main.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        QVERIFY2(window != nullptr, qPrintable(component.errorString()));
        QVERIFY(window->findChild<QQuickItem *>(QStringLiteral("createClipShareLinkButton")));
        QVERIFY(window->findChild<QQuickItem *>(QStringLiteral("clipShareStatus")));
        QTRY_VERIFY(recorder.buffering());
        for (int i = 0; i < 8; ++i) {
            QVERIFY(input.sendVideoFrame(QVideoFrame(sampleFrame(2))));
            QTest::qWait(180);
        }
        const QUrl source = QUrl::fromLocalFile(temporary.filePath("Sample - Blue hour.png"));
        window->setProperty("activeSource", source);
        window->setProperty("activeIsImage", true);
        window->setProperty("activeImageSource", source);
        const QString output = qEnvironmentVariable("MOMENT_UI_PREVIEW_DIR");
        if (!output.isEmpty())
            QVERIFY(QDir().mkpath(output));
        const QString pages[] = {"library", "capture", "preferences", "editor"};
        for (const QSize size : {QSize(1280, 820), QSize(980, 660)}) {
            window->resize(size);
            for (int page = 0; page < 3; ++page) {
                window->setProperty("page", page);
                QTest::qWait(300);
                if (!output.isEmpty()) {
                    const auto image = window->grabWindow();
                    QVERIFY(!image.isNull());
                    QVERIFY(image.save(QDir(output).filePath(QStringLiteral("moment-%1-%2.png").arg(pages[page]).arg(size.width()))));
                }
            }
        }
        discordPresence.setDemoActiveGameName({});
        window->resize(QSize(1280, 820));
        window->setProperty("page", 2);
        QTest::qWait(200);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-preferences-discord-idle-1280.png")));
        auto *frameRate = window->findChild<QQuickItem *>(QStringLiteral("frameRateSetting"));
        QVERIFY(frameRate);
        auto *preferencesScroll = window->findChild<QQuickItem *>(QStringLiteral("preferencesScroll"));
        QVERIFY(preferencesScroll);
        const qreal frameRateSceneY = frameRate->mapToScene(QPointF(0, 0)).y();
        const qreal scrollSceneY = preferencesScroll->mapToScene(QPointF(0, 0)).y();
        preferencesScroll->setProperty("contentY", qMax<qreal>(0,
            preferencesScroll->property("contentY").toReal() + frameRateSceneY - scrollSceneY - 60));
        QTest::qWait(100);
        QTest::mouseClick(window.get(), Qt::LeftButton, Qt::NoModifier,
                          frameRate->mapToScene(QPointF(frameRate->width() / 2, frameRate->height() / 2)).toPoint());
        QTest::qWait(200);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-preferences-dropdown.png")));
        QTest::keyClick(window.get(), Qt::Key_Down);
        QTest::keyClick(window.get(), Qt::Key_Return);
        QTRY_COMPARE(preferences.frameRate(), 60);
        auto *scroll = preferencesScroll;
        const qreal bottom = scroll->property("contentHeight").toReal() - scroll->height();
        QVERIFY(bottom > 0);
        scroll->setProperty("contentY", bottom);
        QTest::qWait(250);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-preferences-bottom.png")));
        auto *sharingCard = window->findChild<QQuickItem *>(QStringLiteral("shareSettingsCard"));
        QVERIFY(sharingCard);
        const qreal sharingCardSceneY = sharingCard->mapToScene(QPointF(0, 0)).y();
        scroll->setProperty("contentY", qMax<qreal>(0,
            scroll->property("contentY").toReal() + sharingCardSceneY - scrollSceneY - 20));
        QTest::qWait(200);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-preferences-sharing-1280.png")));
        window->setProperty("activeSource", QUrl::fromLocalFile(demoVideo));
        window->setProperty("activeIsImage", false);
        window->setProperty("page", 0);
        QTest::qWait(200);
        auto *shareButton = window->findChild<QQuickItem *>(QStringLiteral("createClipShareLinkButton"));
        QVERIFY(shareButton);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-library-share-action-1280.png")));
        QVERIFY(QMetaObject::invokeMethod(window.get(), "openEditor"));
        videoEditor.setDurationMs(8000);
        videoEditor.setInMs(1250);
        videoEditor.setOutMs(6250);
        QTest::qWait(800);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-editor-1280.png")));
        window->setProperty("page", 0);
        window->setProperty("activeSource", QUrl());
        window->setProperty("activeImageSource", QUrl());
        window->setProperty("activeIsImage", false);
        videoEditor.openClip(QString());
        window->setProperty("page", 0);
        auto *editorNavButton = window->findChild<QQuickItem *>(QStringLiteral("editorNavButton"));
        QVERIFY(editorNavButton);
        QVERIFY(editorNavButton->property("enabled").toBool());
        QTest::mouseClick(window.get(), Qt::LeftButton, Qt::NoModifier,
                          editorNavButton->mapToScene(QPointF(editorNavButton->width() / 2,
                                                              editorNavButton->height() / 2)).toPoint());
        QTRY_COMPARE(window->property("page").toInt(), 3);
        auto *editorChooseButton = window->findChild<QQuickItem *>(QStringLiteral("editorEmptyChooseButton"));
        QVERIFY(editorChooseButton);
        QVERIFY(editorChooseButton->property("visible").toBool());
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-editor-empty-1280.png")));
        window->setProperty("page", 0);
        auto *search = window->findChild<QQuickItem *>(QStringLiteral("librarySearch"));
        QVERIFY(search);
        search->setProperty("text", QStringLiteral("no-matching-sample"));
        QCOMPARE(library.clipCount(), 0);
        QTest::qWait(200);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-library-empty.png")));
        window->setProperty("activeSource", QUrl::fromLocalFile(demoVideo));
        window->setProperty("activeIsImage", false);
        QVERIFY(QMetaObject::invokeMethod(window.get(), "openShareSettings"));
        QTRY_COMPARE(window->property("page").toInt(), 2);
        QTRY_VERIFY(window->property("shareSettingsHighlighted").toBool());
        QVERIFY(preferencesScroll->property("contentY").toReal() > 0);
        QTest::qWait(250);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-sharing-settings-focused-1280.png")));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

QTEST_MAIN(MainWindowVisualTest)
#include "main_window_visual_test.moc"
