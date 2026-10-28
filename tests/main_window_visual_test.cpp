#include "library/clip_library_model.h"
#include "library/clip_repository.h"
#include "recording/screen_recorder.h"
#include "recording/recording_preferences.h"
#include "recording/game_monitor.h"

#include <QDir>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
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
        RecordingPreferences preferences(temporary.filePath("settings.ini"));
        GameMonitor gameMonitor(temporary.filePath("settings.ini"));
        QVERIFY(gameMonitor.addProfile(QStringLiteral("demo-game-bin"), QStringLiteral("Demo Game (sample)")));
        QVideoFrameInput input(QVideoFrameFormat(QSize(960, 540), QVideoFrameFormat::Format_ARGB8888));
        ScreenRecorder recorder(library, preferences, temporary.filePath("videos"), temporary.filePath("pictures"), input);
        VisualShortcuts shortcuts;
        VisualTray tray;
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
        engine.rootContext()->setContextProperty("globalShortcuts", &shortcuts);
        engine.rootContext()->setContextProperty("trayController", &tray);
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/MedalLinux/qml/Main.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        QVERIFY2(window != nullptr, qPrintable(component.errorString()));
        QTRY_VERIFY(recorder.buffering());
        for (int i = 0; i < 8; ++i) {
            QVERIFY(input.sendVideoFrame(QVideoFrame(sampleFrame(2))));
            QTest::qWait(180);
        }
        const QUrl source = library.data(library.index(0), ClipLibraryModel::SourceUrlRole).toUrl();
        window->setProperty("activeSource", source);
        window->setProperty("activeIsImage", true);
        window->setProperty("activeImageSource", source);
        const QString output = qEnvironmentVariable("MOMENT_UI_PREVIEW_DIR");
        if (!output.isEmpty())
            QVERIFY(QDir().mkpath(output));
        const QString pages[] = {"library", "capture", "preferences"};
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
        auto *frameRate = window->findChild<QQuickItem *>(QStringLiteral("frameRateSetting"));
        QVERIFY(frameRate);
        QTest::mouseClick(window.get(), Qt::LeftButton, Qt::NoModifier,
                          frameRate->mapToScene(QPointF(frameRate->width() / 2, frameRate->height() / 2)).toPoint());
        QTest::qWait(200);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-preferences-dropdown.png")));
        QTest::keyClick(window.get(), Qt::Key_Down);
        QTest::keyClick(window.get(), Qt::Key_Return);
        QTRY_COMPARE(preferences.frameRate(), 60);
        auto *scroll = window->findChild<QQuickItem *>(QStringLiteral("preferencesScroll"));
        QVERIFY(scroll);
        const qreal bottom = scroll->property("contentHeight").toReal() - scroll->height();
        QVERIFY(bottom > 0);
        scroll->setProperty("contentY", bottom);
        QTest::qWait(250);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-preferences-bottom.png")));
        window->setProperty("activeSource", QUrl());
        window->setProperty("activeImageSource", QUrl());
        window->setProperty("page", 0);
        auto *search = window->findChild<QQuickItem *>(QStringLiteral("librarySearch"));
        QVERIFY(search);
        search->setProperty("text", QStringLiteral("no-matching-sample"));
        QCOMPARE(library.clipCount(), 0);
        QTest::qWait(200);
        if (!output.isEmpty())
            QVERIFY(window->grabWindow().save(QDir(output).filePath("moment-library-empty.png")));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

QTEST_MAIN(MainWindowVisualTest)
#include "main_window_visual_test.moc"
