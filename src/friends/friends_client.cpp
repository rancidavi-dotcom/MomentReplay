#include "friends_client.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMimeDatabase>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrlQuery>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <qt6keychain/keychain.h>

namespace {
const QString kKeychainService = QStringLiteral("Moment Friends");
const QString kKeychainKey = QStringLiteral("account-recovery-key");

QVariantList mapsFromArray(const QJsonArray &array)
{
    QVariantList result;
    result.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (value.isObject())
            result.append(value.toObject().toVariantMap());
    }
    return result;
}

QString apiError(const QJsonObject &object, const QString &fallback)
{
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? fallback : message;
}

QString safeFileName(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    while (value.startsWith(QLatin1Char('.')))
        value.remove(0, 1);
    return value.isEmpty() ? QStringLiteral("Moment clip.mp4") : value.left(140);
}
}

struct FriendsClient::ClipUpload {
    std::unique_ptr<QFile> file;
    QString uploadId;
    QString digest;
    QString fileName;
    QString mime;
    qint64 size = 0;
    qint64 offset = 0;
    int partSize = 1'500'000;
    int partNumber = 0;
    QByteArray currentPart;
};

struct FriendsClient::ClipDownload {
    std::unique_ptr<QSaveFile> file;
    std::unique_ptr<QCryptographicHash> hash;
    QVariantMap metadata;
    QString id;
    QString path;
    qint64 totalSize = 0;
    qint64 received = 0;
    int partSize = 1'500'000;
    int partNumber = 0;
};

FriendsClient::FriendsClient(const QString &settingsFile, const QString &apiBaseUrl,
                             QObject *parent, bool restoreSavedAccount)
    : QObject(parent), m_settings(settingsFile, QSettings::IniFormat),
      m_apiBaseUrl(QUrl(apiBaseUrl))
{
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_realtimeRetryTimer.stop();
        m_realtimeRetryDelayMs = 2000;
        setStatus(QStringLiteral("Moment Friends connected"));
        refreshFriends();
        refreshInbox();
        if (!m_selectedFriendId.isEmpty())
            refreshClips();
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        if (m_signedIn && !m_realtimeRetryTimer.isActive()) {
            setStatus(QStringLiteral("Moment Friends disconnected; reconnecting…"));
            m_realtimeRetryTimer.start(m_realtimeRetryDelayMs);
            m_realtimeRetryDelayMs = qMin(m_realtimeRetryDelayMs * 2, 30000);
        }
    });
    connect(&m_socket, &QWebSocket::textMessageReceived,
            this, &FriendsClient::handleRealtimeMessage);
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_signedIn)
            setStatus(QStringLiteral("Moment Friends connection failed: %1").arg(m_socket.errorString()));
    });
    m_realtimeRetryTimer.setSingleShot(true);
    connect(&m_realtimeRetryTimer, &QTimer::timeout, this, &FriendsClient::openRealtime);
    m_inboxTimer.setInterval(60000);
    connect(&m_inboxTimer, &QTimer::timeout, this, [this] {
        if (m_signedIn) {
            refreshFriends();
            refreshInbox();
        }
    });
    m_inboxTimer.start();
    if (configured() && restoreSavedAccount)
        loadSavedAccount();
    else if (!configured())
        setStatus(QStringLiteral("Moment Friends service is not configured in this build."));
}

void FriendsClient::preparePreviewData()
{
    m_signedIn = true;
    m_username = QStringLiteral("Moment player");
    m_userId = QStringLiteral("preview-local-user");
    m_friendCode = QStringLiteral("M9Q3X7Z2");
    m_selectedFriendId = QStringLiteral("preview-friend");
    m_selectedFriendName = QStringLiteral("RivalPlayer");
    QVariantMap friendItem;
    friendItem.insert(QStringLiteral("id"), m_selectedFriendId);
    friendItem.insert(QStringLiteral("username"), m_selectedFriendName);
    friendItem.insert(QStringLiteral("online"), true);
    m_friends = {friendItem};
    QVariantMap clipItem;
    clipItem.insert(QStringLiteral("id"), QStringLiteral("preview-shared-video"));
    clipItem.insert(QStringLiteral("sender_id"), m_selectedFriendId);
    clipItem.insert(QStringLiteral("recipient_id"), m_userId);
    clipItem.insert(QStringLiteral("clip_name"), QStringLiteral("match-highlight.mp4"));
    clipItem.insert(QStringLiteral("clip_size"), 4 * 1024 * 1024);
    clipItem.insert(QStringLiteral("delivery_status"), QStringLiteral("available"));
    clipItem.insert(QStringLiteral("expires_at"), QStringLiteral("today · 18:45"));
    m_clips = {clipItem};
    setStatus(QStringLiteral("Preview data · no real account or video is used"));
    emit stateChanged();
}

bool FriendsClient::configured() const
{
    return m_apiBaseUrl.isValid()
        && (m_apiBaseUrl.scheme() == QStringLiteral("https")
            || m_apiBaseUrl.scheme() == QStringLiteral("http"))
        && !m_apiBaseUrl.host().isEmpty();
}

QUrl FriendsClient::apiUrl(const QString &path) const
{
    QUrl url = m_apiBaseUrl;
    QString basePath = url.path();
    while (basePath.endsWith(QLatin1Char('/')))
        basePath.chop(1);
    const int queryIndex = path.indexOf(QLatin1Char('?'));
    const QString endpointPath = queryIndex < 0 ? path : path.left(queryIndex);
    url.setPath(basePath + QStringLiteral("/api/v1") + endpointPath);
    if (queryIndex >= 0)
        url.setQuery(QUrlQuery(path.mid(queryIndex + 1)));
    return url;
}

bool FriendsClient::busy() const { return m_busy; }
bool FriendsClient::signedIn() const { return m_signedIn; }
bool FriendsClient::restoringSession() const { return m_restoringSession; }
QString FriendsClient::username() const { return m_username; }
QString FriendsClient::friendCode() const { return m_friendCode; }
QString FriendsClient::recoveryKey() const { return m_recoveryKey; }
QString FriendsClient::statusMessage() const { return m_statusMessage; }
QString FriendsClient::selectedFriendId() const { return m_selectedFriendId; }
QString FriendsClient::selectedFriendName() const { return m_selectedFriendName; }
QVariantList FriendsClient::friends() const { return m_friends; }
QVariantList FriendsClient::incomingRequests() const { return m_incomingRequests; }
QVariantList FriendsClient::outgoingRequests() const { return m_outgoingRequests; }
QVariantList FriendsClient::clips() const { return m_clips; }
int FriendsClient::transferProgress() const { return m_transferProgress; }
QString FriendsClient::transferStatus() const { return m_transferStatus; }

QString FriendsClient::makeRecoveryKey()
{
    QByteArray random(32, Qt::Uninitialized);
    for (char &byte : random)
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(random.toHex());
}

void FriendsClient::request(const QByteArray &method, const QString &path,
                            const QJsonObject &body, bool authenticated,
                            JsonCallback callback)
{
    if (!configured()) {
        callback(false, {{QStringLiteral("error"), QJsonObject{{QStringLiteral("message"),
            QStringLiteral("Moment Friends service is not configured.")}}}});
        return;
    }
    QNetworkRequest networkRequest(apiUrl(path));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Accept", "application/json");
    networkRequest.setTransferTimeout(20000);
    if (authenticated && !m_authKey.isEmpty())
        networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + m_authKey.toLatin1());

    QNetworkReply *reply = nullptr;
    if (method == "GET")
        reply = m_network.get(networkRequest);
    else if (method == "POST")
        reply = m_network.post(networkRequest, QJsonDocument(body).toJson(QJsonDocument::Compact));
    else
        reply = m_network.sendCustomRequest(networkRequest, method,
            body.isEmpty() ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact));

    ++m_pendingRequests;
    const bool wasBusy = m_busy;
    m_busy = true;
    if (!wasBusy)
        emit stateChanged();
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        if (!ok && response.isEmpty()) {
            const QString message = reply->errorString();
            callback(false, {{QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), message}}}});
        } else {
            callback(ok, response);
        }
        reply->deleteLater();
        m_pendingRequests = qMax(0, m_pendingRequests - 1);
        const bool nowBusy = m_pendingRequests > 0;
        if (m_busy != nowBusy) {
            m_busy = nowBusy;
            emit stateChanged();
        }
    });
}

void FriendsClient::setStatus(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit stateChanged();
}

void FriendsClient::createAccount(const QString &username)
{
    m_restoringSession = false;
    const QString normalized = username.trimmed();
    if (normalized.size() < 3 || normalized.size() > 24) {
        setStatus(QStringLiteral("Choose a username between 3 and 24 characters."));
        return;
    }
    const QString key = makeRecoveryKey();
    setStatus(QStringLiteral("Creating your Moment Friends account…"));
    request("POST", QStringLiteral("/accounts/register"),
            {{QStringLiteral("username"), normalized}, {QStringLiteral("authKey"), key}}, false,
            [this, normalized, key](bool ok, const QJsonObject &response) {
        if (!ok) {
            setStatus(apiError(response, QStringLiteral("Could not create the account.")));
            return;
        }
        const QJsonObject user = response.value(QStringLiteral("user")).toObject();
        finishSignIn(normalized, key, user, true);
    });
}

void FriendsClient::signIn(const QString &username, const QString &recoveryKey)
{
    m_restoringSession = false;
    const QString normalized = username.trimmed();
    const QString key = recoveryKey.trimmed().toLower();
    if (normalized.isEmpty() || key.size() != 64) {
        setStatus(QStringLiteral("Enter your username and 64-character recovery key."));
        return;
    }
    setStatus(QStringLiteral("Signing in to Moment Friends…"));
    request("POST", QStringLiteral("/accounts/login"),
            {{QStringLiteral("username"), normalized}, {QStringLiteral("authKey"), key}}, false,
            [this, normalized, key](bool ok, const QJsonObject &response) {
        if (!ok) {
            setStatus(apiError(response, QStringLiteral("Could not sign in.")));
            return;
        }
        finishSignIn(normalized, key, response.value(QStringLiteral("user")).toObject(), false);
    });
}

void FriendsClient::finishSignIn(const QString &username, const QString &authKey,
                                 const QJsonObject &user, bool isNewAccount)
{
    m_authKey = authKey;
    m_recoveryKey = isNewAccount ? authKey : QString();
    m_username = user.value(QStringLiteral("username")).toString(username);
    m_userId = user.value(QStringLiteral("id")).toString();
    m_friendCode = user.value(QStringLiteral("friendCode")).toString();
    m_settings.setValue(QStringLiteral("friends/username"), m_username);
    m_settings.sync();
    m_signedIn = true;
    auto *job = new QKeychain::WritePasswordJob(kKeychainService, this);
    job->setKey(kKeychainKey);
    job->setTextData(authKey);
    connect(job, &QKeychain::Job::finished, this, [this, job, isNewAccount, authKey] {
        if (job->error())
            setStatus(QStringLiteral("Signed in, but the recovery key could not be saved in the system keychain: %1")
                          .arg(job->errorString()));
        else
            setStatus(QStringLiteral("Moment Friends signed in"));
        if (isNewAccount)
            emit recoveryKeyCreated(authKey);
        emit stateChanged();
        refreshFriends();
        refreshInbox();
        openRealtime();
        job->deleteLater();
    });
    job->start();
    emit stateChanged();
}

void FriendsClient::loadSavedAccount()
{
    m_restoringSession = true;
    emit stateChanged();
    auto *job = new QKeychain::ReadPasswordJob(kKeychainService, this);
    job->setKey(kKeychainKey);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job] {
        const QString savedUsername = m_settings.value(QStringLiteral("friends/username")).toString();
        if (job->error() || job->textData().isEmpty() || savedUsername.isEmpty()) {
            m_restoringSession = false;
            if (job->error())
                setStatus(QStringLiteral("Could not access the saved Moment Friends session: %1")
                              .arg(job->errorString()));
            emit stateChanged();
            job->deleteLater();
            return;
        }
        m_authKey = job->textData();
        m_username = savedUsername;
        job->deleteLater();
        request("GET", QStringLiteral("/me"), {}, true, [this](bool ok, const QJsonObject &response) {
            if (!ok) {
                m_restoringSession = false;
                const QJsonObject error = response.value(QStringLiteral("error")).toObject();
                const bool invalidCredential = error.value(QStringLiteral("code")).toString()
                    == QStringLiteral("unauthorized");
                if (invalidCredential) {
                    m_authKey.clear();
                    m_username.clear();
                    setStatus(apiError(response, QStringLiteral("Saved Moment Friends login is no longer valid.")));
                } else {
                    setStatus(QStringLiteral("Could not reconnect to Moment Friends. Your saved session is kept; try again when online."));
                }
                emit stateChanged();
                return;
            }
            const QJsonObject user = response.value(QStringLiteral("user")).toObject();
            m_username = user.value(QStringLiteral("username")).toString();
            m_userId = user.value(QStringLiteral("id")).toString();
            m_friendCode = user.value(QStringLiteral("friendCode")).toString();
            m_signedIn = true;
            m_restoringSession = false;
            setStatus(QStringLiteral("Moment Friends signed in"));
            emit stateChanged();
            refreshFriends();
            refreshInbox();
            openRealtime();
        });
    });
    job->start();
}

void FriendsClient::signOut()
{
    m_realtimeRetryTimer.stop();
    m_restoringSession = false;
    m_socket.close();
    m_authKey.clear();
    m_username.clear();
    m_userId.clear();
    m_friendCode.clear();
    m_recoveryKey.clear();
    m_selectedFriendId.clear();
    m_selectedFriendName.clear();
    m_friends.clear();
    m_incomingRequests.clear();
    m_outgoingRequests.clear();
    m_clips.clear();
    m_signedIn = false;
    m_settings.remove(QStringLiteral("friends/username"));
    m_settings.sync();
    auto *job = new QKeychain::DeletePasswordJob(kKeychainService, this);
    job->setKey(kKeychainKey);
    job->start();
    connect(job, &QKeychain::Job::finished, job, &QObject::deleteLater);
    setStatus(QStringLiteral("Signed out of Moment Friends"));
    emit stateChanged();
}

void FriendsClient::refresh()
{
    if (m_signedIn)
        refreshFriends();
    if (m_signedIn && !m_selectedFriendId.isEmpty())
        refreshClips();
}

void FriendsClient::refreshFriends()
{
    if (!m_signedIn)
        return;
    request("GET", QStringLiteral("/friends"), {}, true, [this](bool ok, const QJsonObject &response) {
        if (!ok) {
            setStatus(apiError(response, QStringLiteral("Could not load friends.")));
            return;
        }
        m_friends = mapsFromArray(response.value(QStringLiteral("friends")).toArray());
        m_incomingRequests = mapsFromArray(response.value(QStringLiteral("incoming")).toArray());
        m_outgoingRequests = mapsFromArray(response.value(QStringLiteral("outgoing")).toArray());
        for (const QVariant &value : m_incomingRequests) {
            const QVariantMap request = value.toMap();
            const QString requestId = request.value(QStringLiteral("requestId")).toString();
            const QVariantMap sender = request.value(QStringLiteral("user")).toMap();
            const QString name = sender.value(QStringLiteral("username")).toString();
            if (markNotificationSeen(QStringLiteral("request"), requestId))
                emit incomingNotification(QStringLiteral("Friend request"),
                    (name.isEmpty() ? QStringLiteral("A Moment user") : name)
                        + QStringLiteral(" wants to add you."), false);
        }
        emit stateChanged();
    });
}

void FriendsClient::addFriend(const QString &friendCode)
{
    if (!m_signedIn)
        return;
    const QString code = friendCode.trimmed().toUpper();
    request("POST", QStringLiteral("/friends/requests"),
            {{QStringLiteral("friendCode"), code}}, true, [this](bool ok, const QJsonObject &response) {
        setStatus(ok ? QStringLiteral("You are now friends")
                      : apiError(response, QStringLiteral("Could not add friend.")));
        if (ok)
            refreshFriends();
    });
}

void FriendsClient::acceptRequest(const QString &requestId)
{
    request("POST", QStringLiteral("/friends/requests/%1/accept").arg(requestId), {}, true,
            [this](bool ok, const QJsonObject &response) {
        setStatus(ok ? QStringLiteral("Friend request accepted")
                     : apiError(response, QStringLiteral("Could not accept request.")));
        if (ok)
            refreshFriends();
    });
}

void FriendsClient::declineRequest(const QString &requestId)
{
    request("POST", QStringLiteral("/friends/requests/%1/decline").arg(requestId), {}, true,
            [this](bool ok, const QJsonObject &response) {
        setStatus(ok ? QStringLiteral("Friend request declined")
                     : apiError(response, QStringLiteral("Could not decline request.")));
        if (ok)
            refreshFriends();
    });
}

void FriendsClient::removeFriend(const QString &friendId)
{
    request("DELETE", QStringLiteral("/friends/%1").arg(friendId), {}, true,
            [this, friendId](bool ok, const QJsonObject &response) {
        setStatus(ok ? QStringLiteral("Friend removed")
                     : apiError(response, QStringLiteral("Could not remove friend.")));
        if (ok) {
            if (m_selectedFriendId == friendId) {
                m_selectedFriendId.clear();
                m_selectedFriendName.clear();
                m_clips.clear();
            }
            refreshFriends();
            emit stateChanged();
        }
    });
}

void FriendsClient::deleteClip(const QString &clipId)
{
    if (!m_signedIn || clipId.isEmpty())
        return;
    request("DELETE", QStringLiteral("/clips/%1").arg(clipId), {}, true,
            [this](bool ok, const QJsonObject &response) {
        setStatus(ok ? QStringLiteral("Shared video deleted for both friends")
                     : apiError(response, QStringLiteral("Could not delete shared video.")));
        if (ok)
            refreshClips();
    });
}

void FriendsClient::selectFriend(const QString &friendId)
{
    m_selectedFriendId = friendId;
    m_selectedFriendName = friendName(friendId);
    m_clips.clear();
    emit stateChanged();
    refreshClips();
}

void FriendsClient::refreshClips()
{
    if (!m_signedIn || m_selectedFriendId.isEmpty())
        return;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("with"), m_selectedFriendId);
    request("GET", QStringLiteral("/clips?%1").arg(query.toString(QUrl::FullyEncoded)), {}, true,
            [this](bool ok, const QJsonObject &response) {
        if (!ok) {
            setStatus(apiError(response, QStringLiteral("Could not load shared videos.")));
            return;
        }
        m_clips = mapsFromArray(response.value(QStringLiteral("clips")).toArray());
        QStringList unreadIds;
        int unreadClips = 0;
        for (const QVariant &value : m_clips) {
            const QVariantMap clip = value.toMap();
            if (clip.value(QStringLiteral("recipient_id")).toString() != m_userId
                || !clip.value(QStringLiteral("read_at")).toString().isEmpty())
                continue;
            const QString id = clip.value(QStringLiteral("id")).toString();
            unreadIds.append(id);
            if (markNotificationSeen(QStringLiteral("clip"), id))
                ++unreadClips;
        }
        if (unreadClips) {
            const QString who = m_selectedFriendName.isEmpty() ? QStringLiteral("your friend") : m_selectedFriendName;
            emit incomingNotification(QStringLiteral("Videos waiting"),
                QStringLiteral("%1 new video(s) from %2.").arg(unreadClips).arg(who), false);
        }
        acknowledgeClips(unreadIds);
        emit stateChanged();
    });
}

void FriendsClient::refreshInbox()
{
    if (!m_signedIn)
        return;
    request("GET", QStringLiteral("/inbox"), {}, true, [this](bool ok, const QJsonObject &response) {
        if (!ok) {
            setStatus(apiError(response, QStringLiteral("Could not check for videos sent while you were away.")));
            return;
        }
        const QJsonArray incoming = response.value(QStringLiteral("clips")).toArray();
        QStringList ids;
        QStringList senders;
        int newClips = 0;
        for (const QJsonValue &value : incoming) {
            if (!value.isObject())
                continue;
            const QJsonObject clip = value.toObject();
            const QString id = clip.value(QStringLiteral("id")).toString();
            ids.append(id);
            const QString senderId = clip.value(QStringLiteral("sender_id")).toString();
            const QString sender = clip.value(QStringLiteral("sender_name")).toString();
            if (!senders.contains(sender.isEmpty() ? friendName(senderId) : sender))
                senders.append(sender.isEmpty() ? friendName(senderId) : sender);
            if (markNotificationSeen(QStringLiteral("clip"), id))
                ++newClips;
        }
        if (newClips) {
            QString senderList = senders.mid(0, 3).join(QStringLiteral(", "));
            if (senders.size() > 3)
                senderList += QStringLiteral(" and others");
            emit incomingNotification(QStringLiteral("Videos waiting"),
                QStringLiteral("%1 video(s) are waiting from %2. Open Friends to view them.")
                    .arg(newClips).arg(senderList), false);
        }
        acknowledgeClips(ids);
    });
}

void FriendsClient::acknowledgeClips(const QStringList &clipIds)
{
    if (!m_signedIn || clipIds.isEmpty())
        return;
    QJsonArray ids;
    for (const QString &id : clipIds)
        ids.append(id);
    request("POST", QStringLiteral("/clips/ack"),
            {{QStringLiteral("ids"), ids}}, true,
            [this](bool ok, const QJsonObject &response) {
        if (!ok)
            setStatus(apiError(response, QStringLiteral("Could not update the video inbox.")));
    });
}

void FriendsClient::sendClip(const QUrl &fileUrl)
{
    if (!m_signedIn || m_selectedFriendId.isEmpty() || m_clipUpload) {
        setStatus(QStringLiteral("Select a friend before sending a video."));
        return;
    }
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable() || info.size() <= 0) {
        setStatus(QStringLiteral("The selected video cannot be read."));
        return;
    }
    constexpr qint64 maxClientClip = 20LL * 1024 * 1024;
    if (info.size() > maxClientClip) {
        setStatus(QStringLiteral("Videos must be 20 MiB or smaller."));
        return;
    }
    QMimeDatabase mimeDatabase;
    QString mime = mimeDatabase.mimeTypeForFile(info, QMimeDatabase::MatchExtension).name();
    if (mime == QStringLiteral("application/octet-stream"))
        mime = QStringLiteral("video/mp4");
    if (mime != QStringLiteral("video/mp4") && mime != QStringLiteral("video/webm")
        && mime != QStringLiteral("video/quicktime") && mime != QStringLiteral("video/x-matroska")) {
        setStatus(QStringLiteral("Choose an MP4, WebM, MOV, or Matroska video."));
        return;
    }
    m_transferProgress = 0;
    m_transferStatus = QStringLiteral("Preparing video…");
    setStatus(m_transferStatus);
    ++m_pendingRequests;
    m_busy = true;
    emit stateChanged();

    auto *watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this,
            [this, watcher, path, name = safeFileName(info.fileName()), mime,
             size = info.size(), recipient = m_selectedFriendId] {
        const QByteArray digest = watcher->result();
        watcher->deleteLater();
        m_pendingRequests = qMax(0, m_pendingRequests - 1);
        m_busy = m_pendingRequests > 0;
        if (digest.size() != 64) {
            m_transferStatus.clear();
            setStatus(QStringLiteral("Could not calculate the video's SHA-256."));
            emit stateChanged();
            return;
        }
        QJsonObject body{
            {QStringLiteral("to"), recipient},
            {QStringLiteral("clipName"), name},
            {QStringLiteral("clipSize"), static_cast<double>(size)},
            {QStringLiteral("clipSha256"), QString::fromLatin1(digest)},
            {QStringLiteral("clipMime"), mime},
        };
        request("POST", QStringLiteral("/clips/uploads"), body, true,
                [this, path, name, mime, size, digest = QString::fromLatin1(digest)](bool ok, const QJsonObject &response) {
            if (!ok) {
                m_transferStatus.clear();
                setStatus(apiError(response, QStringLiteral("Could not start the video upload.")));
                emit stateChanged();
                return;
            }
            auto upload = std::make_shared<ClipUpload>();
            upload->uploadId = response.value(QStringLiteral("uploadId")).toString();
            upload->partSize = response.value(QStringLiteral("partSize")).toInt();
            upload->digest = digest;
            upload->fileName = name;
            upload->mime = mime;
            upload->size = size;
            upload->file = std::make_unique<QFile>(path);
            if (upload->uploadId.isEmpty() || upload->partSize <= 0 || upload->partSize > 1'500'000
                || !upload->file->open(QIODevice::ReadOnly) || upload->file->size() != size) {
                m_clipUpload = upload;
                failClipUpload(QStringLiteral("The video changed or could not be opened for upload."));
                return;
            }
            m_clipUpload = std::move(upload);
            m_transferStatus = QStringLiteral("Uploading video…");
            setStatus(m_transferStatus);
            uploadNextClipPart();
        });
    });
    watcher->setFuture(QtConcurrent::run([path] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray{};
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(4 * 1024 * 1024);
            if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
                return QByteArray{};
            hash.addData(chunk);
        }
        return hash.result().toHex();
    }));
}

void FriendsClient::uploadNextClipPart()
{
    const auto upload = m_clipUpload;
    if (!upload)
        return;
    if (upload->offset >= upload->size) {
        request("POST", QStringLiteral("/clips/uploads/") + upload->uploadId, {}, true,
                [this, upload](bool ok, const QJsonObject &response) {
            if (!ok) {
                failClipUpload(apiError(response, QStringLiteral("The video upload could not be finalized.")));
                return;
            }
            m_clipUpload.reset();
            m_transferProgress = 100;
            m_transferStatus = QStringLiteral("Video sent · available for 24 hours");
            setStatus(m_transferStatus);
            refreshClips();
            emit stateChanged();
        });
        return;
    }

    const qint64 remaining = upload->size - upload->offset;
    const qint64 expected = qMin<qint64>(upload->partSize, remaining);
    upload->currentPart = upload->file->read(expected);
    if (upload->currentPart.size() != expected) {
        failClipUpload(QStringLiteral("The video file could not be read completely."));
        return;
    }
    ++upload->partNumber;
    QNetworkRequest networkRequest(apiUrl(QStringLiteral("/clips/uploads/%1/parts/%2")
                                               .arg(upload->uploadId)
                                               .arg(upload->partNumber)));
    networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + m_authKey.toLatin1());
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    networkRequest.setTransferTimeout(120000);
    const bool wasBusy = m_busy;
    ++m_pendingRequests;
    m_busy = true;
    QNetworkReply *reply = m_network.put(networkRequest, upload->currentPart);
    connect(reply, &QNetworkReply::finished, this, [this, reply, upload, wasBusy] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const int savedPart = response.value(QStringLiteral("partNumber")).toInt();
        const int savedSize = response.value(QStringLiteral("size")).toInt();
        if (!ok || savedPart != upload->partNumber || savedSize != upload->currentPart.size()
            || m_clipUpload != upload) {
            const QString why = apiError(response, reply->errorString().isEmpty()
                ? QStringLiteral("A video part failed to upload.") : reply->errorString());
            reply->deleteLater();
            m_pendingRequests = qMax(0, m_pendingRequests - 1);
            m_busy = m_pendingRequests > 0;
            Q_UNUSED(wasBusy)
            failClipUpload(why);
            return;
        }
        upload->offset += upload->currentPart.size();
        upload->currentPart.clear();
        m_transferProgress = static_cast<int>((upload->offset * 100) / upload->size);
        m_transferStatus = QStringLiteral("Uploading video · %1%").arg(m_transferProgress);
        setStatus(m_transferStatus);
        reply->deleteLater();
        m_pendingRequests = qMax(0, m_pendingRequests - 1);
        m_busy = m_pendingRequests > 0;
        emit stateChanged();
        QTimer::singleShot(0, this, &FriendsClient::uploadNextClipPart);
    });
    if (!wasBusy)
        emit stateChanged();
}

void FriendsClient::failClipUpload(const QString &message)
{
    const QString uploadId = m_clipUpload ? m_clipUpload->uploadId : QString();
    m_clipUpload.reset();
    m_transferProgress = 0;
    m_transferStatus.clear();
    setStatus(message);
    emit stateChanged();
    if (!uploadId.isEmpty())
        request("DELETE", QStringLiteral("/clips/uploads/") + uploadId, {}, true,
                [](bool, const QJsonObject &) {});
}

void FriendsClient::downloadClip(const QString &clipId)
{
    if (!m_signedIn || !clipId.contains(QLatin1Char('-'))) {
        setStatus(QStringLiteral("Sign in and choose a valid shared video first."));
        return;
    }
    if (m_clipDownload) {
        setStatus(QStringLiteral("A video is already downloading."));
        return;
    }
    QVariantMap metadata;
    for (const QVariant &value : m_clips) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == clipId) {
            metadata = candidate;
            break;
        }
    }
    if (metadata.isEmpty() || metadata.value(QStringLiteral("delivery_status")).toString() != QStringLiteral("available")) {
        setStatus(QStringLiteral("This video is no longer available."));
        return;
    }
    QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (movies.isEmpty())
        movies = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString directory = QDir(movies).filePath(QStringLiteral("Moment/Received"));
    if (!QDir().mkpath(directory)) {
        setStatus(QStringLiteral("Could not create the received-video folder."));
        return;
    }
    const QString baseName = safeFileName(metadata.value(QStringLiteral("clip_name")).toString());
    const QString filename = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
        + QLatin1Char('-') + baseName;
    const QString path = QDir(directory).filePath(filename);
    auto download = std::make_shared<ClipDownload>();
    download->file = std::make_unique<QSaveFile>(path);
    download->hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    download->metadata = metadata;
    download->id = clipId;
    download->path = path;
    download->totalSize = metadata.value(QStringLiteral("clip_size")).toLongLong();
    if (download->totalSize <= 0 || download->totalSize > 20LL * 1024 * 1024
        || !download->file->open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("Could not create a file for the received video."));
        return;
    }
    m_clipDownload = std::move(download);
    m_transferProgress = 0;
    m_transferStatus = QStringLiteral("Downloading video…");
    setStatus(m_transferStatus);
    downloadNextClipPart();
}

void FriendsClient::downloadNextClipPart()
{
    const auto download = m_clipDownload;
    if (!download)
        return;
    if (download->received >= download->totalSize) {
        const bool valid = download->received == download->totalSize
            && QString::fromLatin1(download->hash->result().toHex())
                == download->metadata.value(QStringLiteral("clip_sha256")).toString();
        if (!valid) {
            download->file->cancelWriting();
            m_clipDownload.reset();
            m_transferStatus.clear();
            setStatus(QStringLiteral("Downloaded video failed its integrity check."));
            return;
        }
        if (!download->file->commit()) {
            m_clipDownload.reset();
            m_transferStatus.clear();
            setStatus(QStringLiteral("Could not save the received video to disk."));
            return;
        }
        const QString path = download->path;
        m_clipDownload.reset();
        m_transferProgress = 100;
        m_transferStatus = QStringLiteral("Video downloaded");
        setStatus(m_transferStatus);
        emit clipDownloaded(path);
        return;
    }

    const int partNumber = download->partNumber + 1;
    QNetworkRequest networkRequest(apiUrl(QStringLiteral("/clips/%1/parts/%2")
                                               .arg(download->id).arg(partNumber)));
    networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + m_authKey.toLatin1());
    networkRequest.setTransferTimeout(30000);
    const bool wasBusy = m_busy;
    ++m_pendingRequests;
    m_busy = true;
    QNetworkReply *reply = m_network.get(networkRequest);
    connect(reply, &QNetworkReply::finished, this, [this, reply, download, partNumber, wasBusy] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        const qint64 expected = qMin<qint64>(download->partSize, download->totalSize - download->received);
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200
            && bytes.size() == expected && m_clipDownload == download;
        if (!ok) {
            const QJsonObject errorObject = QJsonDocument::fromJson(bytes).object();
            download->file->cancelWriting();
            if (m_clipDownload == download)
                m_clipDownload.reset();
            m_transferStatus.clear();
            setStatus(apiError(errorObject, reply->errorString().isEmpty()
                ? QStringLiteral("A video part could not be downloaded.") : reply->errorString()));
        } else if (download->file->write(bytes) != bytes.size()) {
            download->file->cancelWriting();
            m_clipDownload.reset();
            m_transferStatus.clear();
            setStatus(QStringLiteral("Could not write the received video to disk."));
        } else {
            download->hash->addData(bytes);
            download->received += bytes.size();
            download->partNumber = partNumber;
            m_transferProgress = static_cast<int>((download->received * 100) / download->totalSize);
            m_transferStatus = QStringLiteral("Downloading video · %1%").arg(m_transferProgress);
            setStatus(m_transferStatus);
        }
        reply->deleteLater();
        m_pendingRequests = qMax(0, m_pendingRequests - 1);
        m_busy = m_pendingRequests > 0;
        emit stateChanged();
        if (ok)
            QTimer::singleShot(0, this, &FriendsClient::downloadNextClipPart);
        Q_UNUSED(wasBusy)
    });
    if (!wasBusy)
        emit stateChanged();
}

void FriendsClient::openRealtime()
{
    if (!m_signedIn || !configured() || m_socket.state() == QAbstractSocket::ConnectedState
        || m_socket.state() == QAbstractSocket::ConnectingState)
        return;
    QUrl url = m_apiBaseUrl;
    QString basePath = url.path();
    while (basePath.endsWith(QLatin1Char('/')))
        basePath.chop(1);
    url.setPath(basePath + QStringLiteral("/api/v1/realtime"));
    if (url.scheme() == QStringLiteral("https"))
        url.setScheme(QStringLiteral("wss"));
    else
        url.setScheme(QStringLiteral("ws"));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_authKey.toLatin1());
    m_socket.open(request);
}

void FriendsClient::handleRealtimeMessage(const QString &rawMessage)
{
    const QJsonObject event = QJsonDocument::fromJson(rawMessage.toUtf8()).object();
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("friend.request")) {
        refreshFriends();
    } else if (type == QStringLiteral("friend.accepted")) {
        const QJsonObject friendUser = event.value(QStringLiteral("friend")).toObject();
        const QString name = friendUser.value(QStringLiteral("username")).toString(QStringLiteral("A Moment user"));
        incomingNotification(QStringLiteral("Friend request accepted"), name + QStringLiteral(" is now your friend."), false);
        refreshFriends();
    } else if (type == QStringLiteral("friend.declined")) {
        incomingNotification(QStringLiteral("Friend request declined"), QStringLiteral("Your friend request was declined."), true);
        refreshFriends();
    } else if (type == QStringLiteral("clip.new")) {
        const QJsonObject clip = event.value(QStringLiteral("clip")).toObject();
        const QString senderId = clip.value(QStringLiteral("sender_id")).toString();
        const QString name = friendName(senderId);
        const QString id = clip.value(QStringLiteral("id")).toString();
        if (markNotificationSeen(QStringLiteral("clip"), id))
            incomingNotification(QStringLiteral("Video waiting"),
                QStringLiteral("%1 shared %2 with you.").arg(name, clip.value(QStringLiteral("clip_name")).toString()), false);
        acknowledgeClips(QStringList{id});
        if (senderId == m_selectedFriendId)
            refreshClips();
    } else if (type == QStringLiteral("signal")) {
        const QString signalType = event.value(QStringLiteral("signalType")).toString();
        if (signalType == QStringLiteral("clip-accepted")) {
            const QString name = friendName(event.value(QStringLiteral("from")).toString());
            incomingNotification(QStringLiteral("Friend is ready"), name + QStringLiteral(" accepted your clip transfer."), false);
        } else if (signalType == QStringLiteral("clip-complete")) {
            const QString name = friendName(event.value(QStringLiteral("from")).toString());
            incomingNotification(QStringLiteral("Clip received"), name + QStringLiteral(" finished sending a clip."), false);
        }
    }
}

QString FriendsClient::friendName(const QString &friendId) const
{
    for (const QVariant &value : m_friends) {
        const QVariantMap friendMap = value.toMap();
        if (friendMap.value(QStringLiteral("id")).toString() == friendId)
            return friendMap.value(QStringLiteral("username")).toString().isEmpty()
                ? QStringLiteral("A Moment friend")
                : friendMap.value(QStringLiteral("username")).toString();
    }
    return QStringLiteral("A Moment friend");
}

bool FriendsClient::markNotificationSeen(const QString &kind, const QString &id)
{
    if (id.isEmpty())
        return false;
    QStringList seen = m_settings.value(QStringLiteral("friends/notifiedItems")).toStringList();
    const QString key = kind + QLatin1Char(':') + id;
    if (seen.contains(key))
        return false;
    seen.append(key);
    constexpr int maxRemembered = 500;
    if (seen.size() > maxRemembered)
        seen = seen.mid(seen.size() - maxRemembered);
    m_settings.setValue(QStringLiteral("friends/notifiedItems"), seen);
    m_settings.sync();
    return true;
}

void FriendsClient::copyRecoveryKey()
{
    const QString key = m_recoveryKey.isEmpty() ? m_authKey : m_recoveryKey;
    QGuiApplication::clipboard()->setText(key);
    setStatus(QStringLiteral("Recovery key copied. Keep it private and store a backup."));
}

void FriendsClient::copyFriendCode()
{
    if (m_friendCode.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(m_friendCode);
    setStatus(QStringLiteral("Friend code copied"));
}

void FriendsClient::dismissRecoveryKey()
{
    if (m_recoveryKey.isEmpty())
        return;
    m_recoveryKey.clear();
    emit stateChanged();
}
