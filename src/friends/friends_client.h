#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QWebSocket>
#include <QUrl>
#include <memory>

#include <functional>

class QNetworkReply;
class QJsonObject;

class FriendsClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY stateChanged)
    Q_PROPERTY(QString username READ username NOTIFY stateChanged)
    Q_PROPERTY(QString friendCode READ friendCode NOTIFY stateChanged)
    Q_PROPERTY(QString recoveryKey READ recoveryKey NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(QString selectedFriendId READ selectedFriendId NOTIFY stateChanged)
    Q_PROPERTY(QString selectedFriendName READ selectedFriendName NOTIFY stateChanged)
    Q_PROPERTY(QVariantList friends READ friends NOTIFY stateChanged)
    Q_PROPERTY(QVariantList incomingRequests READ incomingRequests NOTIFY stateChanged)
    Q_PROPERTY(QVariantList outgoingRequests READ outgoingRequests NOTIFY stateChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY stateChanged)
    Q_PROPERTY(int transferProgress READ transferProgress NOTIFY stateChanged)
    Q_PROPERTY(QString transferStatus READ transferStatus NOTIFY stateChanged)

public:
    explicit FriendsClient(const QString &settingsFile, const QString &apiBaseUrl,
                           QObject *parent = nullptr);

    bool configured() const;
    bool busy() const;
    bool signedIn() const;
    QString username() const;
    QString friendCode() const;
    QString recoveryKey() const;
    QString statusMessage() const;
    QString selectedFriendId() const;
    QString selectedFriendName() const;
    QVariantList friends() const;
    QVariantList incomingRequests() const;
    QVariantList outgoingRequests() const;
    QVariantList clips() const;
    int transferProgress() const;
    QString transferStatus() const;

    Q_INVOKABLE void createAccount(const QString &username);
    Q_INVOKABLE void signIn(const QString &username, const QString &recoveryKey);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void addFriend(const QString &friendCode);
    Q_INVOKABLE void acceptRequest(const QString &requestId);
    Q_INVOKABLE void declineRequest(const QString &requestId);
    Q_INVOKABLE void removeFriend(const QString &friendId);
    Q_INVOKABLE void selectFriend(const QString &friendId);
    Q_INVOKABLE void sendClip(const QUrl &fileUrl);
    Q_INVOKABLE void downloadClip(const QString &clipId);
    Q_INVOKABLE void copyRecoveryKey();
    Q_INVOKABLE void copyFriendCode();
    Q_INVOKABLE void dismissRecoveryKey();

signals:
    void stateChanged();
    void recoveryKeyCreated(const QString &key);
    void incomingNotification(const QString &title, const QString &message, bool warning);
    void clipDownloaded(const QString &path);

private:
    using JsonCallback = std::function<void(bool, const QJsonObject &)>;
    void request(const QByteArray &method, const QString &path, const QJsonObject &body,
                 bool authenticated, JsonCallback callback);
    void setStatus(const QString &message);
    void finishSignIn(const QString &username, const QString &authKey,
                      const QJsonObject &user, bool isNewAccount);
    void loadSavedAccount();
    void openRealtime();
    void handleRealtimeMessage(const QString &message);
    void refreshFriends();
    void refreshClips();
    void refreshInbox();
    void acknowledgeClips(const QStringList &clipIds);
    bool markNotificationSeen(const QString &kind, const QString &id);
    QString friendName(const QString &friendId) const;
    static QString makeRecoveryKey();
    void uploadNextClipPart();
    void failClipUpload(const QString &message);
    void downloadNextClipPart();
    QUrl apiUrl(const QString &path) const;

    struct ClipUpload;

    QNetworkAccessManager m_network;
    QWebSocket m_socket;
    QTimer m_inboxTimer;
    QSettings m_settings;
    QUrl m_apiBaseUrl;
    QString m_authKey;
    QString m_username;
    QString m_userId;
    QString m_friendCode;
    QString m_recoveryKey;
    QString m_statusMessage;
    QString m_selectedFriendId;
    QString m_selectedFriendName;
    QVariantList m_friends;
    QVariantList m_incomingRequests;
    QVariantList m_outgoingRequests;
    QVariantList m_clips;
    std::shared_ptr<ClipUpload> m_clipUpload;
    struct ClipDownload;
    std::shared_ptr<ClipDownload> m_clipDownload;
    int m_transferProgress = 0;
    QString m_transferStatus;
    int m_pendingRequests = 0;
    bool m_busy = false;
    bool m_signedIn = false;
};
