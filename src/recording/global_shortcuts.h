#pragma once

#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QDBusMessage;

class GlobalShortcuts final : public QObject {
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
    void handleCreateSessionResponse(uint response, const QVariantMap &results);
    void handleBindShortcutsResponse(uint response, const QVariantMap &results);
    void handleShortcutsChanged(const QDBusMessage &message);
    void handleSessionClosed(const QDBusMessage &message);
    void handleActivated(const QDBusObjectPath &session, const QString &shortcutId,
                         qulonglong timestamp, const QVariantMap &options);

private:
    void setStatus(const QString &message);
    void connectRequestResponse(const QString &requestPath, const char *slot);
    void disconnectRequestResponse(const QString &requestPath, const char *slot);
    QString requestPathForToken(const QString &token) const;
    void createSession();
    void bindReplayShortcut();
    void resetSession(bool closeRemote);
    void applyShortcuts(const QVariantMap &results, const QString &statusPrefix,
                        bool replaceCurrent);

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
