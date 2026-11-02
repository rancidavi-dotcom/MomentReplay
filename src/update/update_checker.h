#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QPointer>
#include <QUrl>

class UpdateChecker final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool canInstallUpdate READ canInstallUpdate NOTIFY stateChanged)
    Q_PROPERTY(bool installSupported READ installSupported NOTIFY stateChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    Q_PROPERTY(int downloadPercent READ downloadPercent NOTIFY stateChanged)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)

public:
    explicit UpdateChecker(const QString &settingsFile, const QString &currentVersion,
                           bool enabled = true, QObject *parent = nullptr,
                           const QUrl &latestReleaseApiForTest = {},
                           const QString &installPathForTest = {});

    bool autoCheck() const;
    bool checking() const;
    bool updateAvailable() const;
    bool canInstallUpdate() const;
    bool installSupported() const;
    bool downloading() const;
    int downloadPercent() const;
    bool restartRequired() const;
    QString currentVersion() const;
    QString latestVersion() const;
    QString status() const;

    Q_INVOKABLE void setAutoCheck(bool enabled);
    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE void restartToApply();

signals:
    void autoCheckChanged();
    void stateChanged();

private:
    void checkIfDue();
    void handleReply(class QNetworkReply *reply);
    void handleAssetReply(class QNetworkReply *reply);
    void refreshInstallSupport();

    QSettings m_settings;
    QNetworkAccessManager m_network;
    QTimer m_periodicTimer;
    QFile m_downloadFile;
    QCryptographicHash m_downloadHash{QCryptographicHash::Sha256};
    QPointer<class QNetworkReply> m_downloadReply;
    QString m_currentVersion;
    QString m_latestVersion;
    QString m_status = QStringLiteral("Not checked yet");
    QString m_releaseUrl;
    QString m_assetUrl;
    QString m_assetDigest;
    QString m_downloadPath;
    QUrl m_latestReleaseApi;
    QString m_installPathForTest;
    bool m_autoCheck = true;
    bool m_enabled;
    bool m_checking = false;
    bool m_updateAvailable = false;
    bool m_installSupported = false;
    bool m_canInstallUpdate = false;
    bool m_downloading = false;
    bool m_restartRequired = false;
    bool m_testEndpoints = false;
    int m_downloadPercent = 0;
};
