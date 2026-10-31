#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#else
#include <QDBusObjectPath>
#include <QDBusVariant>
#endif
#include <QObject>
#include <QString>
#include <QVariantMap>

class QDBusMessage;

class GlobalShortcuts final : public QObject
#ifdef Q_OS_WIN
    , public QAbstractNativeEventFilter
#endif
{
    Q_OBJECT
    Q_PROPERTY(bool portalAvailable READ portalAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY stateChanged)
    Q_PROPERTY(bool screenshotRegistered READ screenshotRegistered NOTIFY stateChanged)
    Q_PROPERTY(bool allRegistered READ allRegistered NOTIFY stateChanged)
    Q_PROPERTY(bool requesting READ requesting NOTIFY stateChanged)
    Q_PROPERTY(QString triggerDescription READ triggerDescription NOTIFY stateChanged)
    Q_PROPERTY(QString screenshotTriggerDescription READ screenshotTriggerDescription NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)

public:
    explicit GlobalShortcuts(QObject *parent = nullptr);
    ~GlobalShortcuts() override;
#ifdef Q_OS_WIN
    explicit GlobalShortcuts(const QString &settingsFile, QObject *parent = nullptr);
    Q_INVOKABLE bool setCaptureShortcuts(const QString &replay, const QString &screenshot);
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

    bool portalAvailable() const;
    bool registered() const;
    bool screenshotRegistered() const;
    bool allRegistered() const;
    bool requesting() const;
    QString triggerDescription() const;
    QString screenshotTriggerDescription() const;
    QString statusMessage() const;

    Q_INVOKABLE void requestCaptureShortcuts();
    Q_INVOKABLE void reportReplayUnavailable();

signals:
    void stateChanged();
    void saveReplayRequested();
    void saveScreenshotRequested();

private slots:
#ifndef Q_OS_WIN
    void handleCreateSessionResponse(uint response, const QVariantMap &results);
    void handleBindShortcutsResponse(uint response, const QVariantMap &results);
    void handleShortcutsChanged(const QDBusMessage &message);
    void handleSessionClosed(const QDBusMessage &message);
    void handleActivated(const QDBusObjectPath &session, const QString &shortcutId,
                         qulonglong timestamp, const QVariantMap &options);

#endif

private:
    void setStatus(const QString &message);
#ifndef Q_OS_WIN
    void connectRequestResponse(const QString &requestPath, const char *slot);
    void disconnectRequestResponse(const QString &requestPath, const char *slot);
    QString requestPathForToken(const QString &token) const;
    void createSession();
    void bindReplayShortcut();
    void resetSession(bool closeRemote);
    void applyShortcuts(const QVariantMap &results, const QString &statusPrefix,
                        bool replaceCurrent);

#else
    QString m_settingsFile;
    int m_replayId = 0;
    int m_screenshotId = 0;
    void releaseHotkeys();
    bool registerBindings(const QString &replay, const QString &screenshot);
#endif

    bool m_portalAvailable = false;
    bool m_registered = false;
    bool m_screenshotRegistered = false;
    bool m_requesting = false;
    QString m_sessionPath;
    QString m_createRequestPath;
    QString m_bindRequestPath;
    quint64 m_generation = 0;
    QString m_triggerDescription = QStringLiteral("F8");
    QString m_screenshotTriggerDescription = QStringLiteral("F9");
    QString m_statusMessage = QStringLiteral("Global shortcut is not enabled");
};
