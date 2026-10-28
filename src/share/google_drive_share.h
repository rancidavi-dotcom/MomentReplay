#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QPointer>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QUrl>

#include <functional>
#include <memory>

class QNetworkReply;

class GoogleDriveShare final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString lastShareUrl READ lastShareUrl NOTIFY lastShareUrlChanged)

public:
    explicit GoogleDriveShare(QObject *parent = nullptr);
    GoogleDriveShare(const QString &keychainService, QObject *parent = nullptr);

    bool configured() const;
    bool connected() const;
    bool busy() const;
    QString statusMessage() const;
    QString lastShareUrl() const;

    Q_INVOKABLE void shareClip(const QString &path);
    Q_INVOKABLE void shareClipUrl(const QUrl &url);
    Q_INVOKABLE void copyLastShareUrl();
    Q_INVOKABLE void disconnectAccount();

signals:
    void configuredChanged();
    void connectedChanged();
    void busyChanged();
    void statusMessageChanged();
    void lastShareUrlChanged();
    void shareCompleted(const QString &url);
    void shareFailed(const QString &message);

private:
    void loadSavedToken();
    void setBusy(bool busy);
    void setStatusMessage(const QString &message);
    void setConnected(bool connected);
    void beginAuthorization();
    void handleAuthorizationRequest();
    void exchangeAuthorizationCode(const QString &code);
    void refreshAccessToken(std::function<void(bool)> continuation);
    void ensureAccessToken(std::function<void(bool)> continuation);
    void saveRefreshToken(const QString &token, std::function<void(bool)> continuation = {});
    void uploadClip(const QString &path);
    void finishClipTranscode(int exitCode, QProcess::ExitStatus exitStatus);
    void uploadPreparedClip(const QString &path);
    void requestSharePermission(const QString &fileId);
    void fetchShareLink(const QString &fileId);
    void fail(const QString &message);
    void cleanupRemoteFile(const QString &fileId);

    QNetworkAccessManager m_network;
    QTcpServer m_oauthServer;
    QProcess m_transcoder;
    std::unique_ptr<QTemporaryDir> m_shareTempDir;
    bool m_credentialsLoaded = false;
    bool m_connected = false;
    bool m_busy = false;
    QString m_statusMessage = QStringLiteral("A share link lets anyone who receives it view this clip.");
    QString m_keychainService;
    QString m_lastShareUrl;
    QString m_refreshToken;
    QString m_accessToken;
    QString m_pendingPath;
    QString m_transcodeOutputPath;
    QString m_originalClipName;
    QString m_oauthState;
    QString m_codeVerifier;
    QUrl m_redirectUrl;
    qint64 m_accessTokenExpiresAtMs = 0;
    QString m_uploadedFileId;
};
