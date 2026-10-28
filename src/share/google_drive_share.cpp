#include "google_drive_share.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QProcess>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QTcpSocket>
#include <QTimer>
#include <QClipboard>

#include <utility>

#include <qt6keychain/keychain.h>

#ifndef MOMENT_GOOGLE_OAUTH_CLIENT_ID
#define MOMENT_GOOGLE_OAUTH_CLIENT_ID ""
#endif
#ifndef MOMENT_GOOGLE_OAUTH_CLIENT_SECRET
#define MOMENT_GOOGLE_OAUTH_CLIENT_SECRET ""
#endif

namespace {

const QString kClientId = QString::fromLatin1(MOMENT_GOOGLE_OAUTH_CLIENT_ID);
const QString kClientSecret = QString::fromLatin1(MOMENT_GOOGLE_OAUTH_CLIENT_SECRET);
const QString kKeychainService = QStringLiteral("Moment");
const QString kKeychainKey = QStringLiteral("google-drive-refresh-token");
const QString kDriveFileScope = QStringLiteral("https://www.googleapis.com/auth/drive.file");

QString randomUrlSafe(int byteCount)
{
    QByteArray bytes(byteCount, Qt::Uninitialized);
    auto *random = QRandomGenerator::system();
    for (char &byte : bytes)
        byte = static_cast<char>(random->bounded(256));
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding
                                              | QByteArray::OmitTrailingEquals));
}

QByteArray formBody(const QMap<QString, QString> &values)
{
    QUrlQuery query;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        query.addQueryItem(it.key(), it.value());
    return query.query(QUrl::FullyEncoded).toUtf8();
}

QString networkErrorText(QNetworkReply *reply, const QJsonObject &json = {})
{
    QString detail = json.value(QStringLiteral("error_description")).toString();
    if (detail.isEmpty())
        detail = json.value(QStringLiteral("message")).toString();
    if (detail.isEmpty())
        detail = reply->errorString();
    if (detail.isEmpty())
        detail = QStringLiteral("Unknown network error");
    detail = detail.simplified();
    if (detail.size() > 220)
        detail = detail.left(217) + QStringLiteral("…");

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return status > 0 ? QStringLiteral("HTTP %1: %2").arg(status).arg(detail) : detail;
}

} // namespace

GoogleDriveShare::GoogleDriveShare(QObject *parent)
    : GoogleDriveShare(kKeychainService, parent)
{
}

GoogleDriveShare::GoogleDriveShare(const QString &keychainService, QObject *parent)
    : QObject(parent), m_keychainService(keychainService)
{
    connect(&m_oauthServer, &QTcpServer::newConnection,
            this, &GoogleDriveShare::handleAuthorizationRequest);
    connect(&m_transcoder,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &GoogleDriveShare::finishClipTranscode);
    connect(&m_transcoder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_busy) {
            m_shareTempDir.reset();
            fail(QStringLiteral("FFmpeg could not start to prepare a Drive-compatible video."));
        }
    });
}

bool GoogleDriveShare::configured() const { return !kClientId.isEmpty(); }
bool GoogleDriveShare::connected() const { return m_connected; }
bool GoogleDriveShare::busy() const { return m_busy; }
QString GoogleDriveShare::statusMessage() const { return m_statusMessage; }
QString GoogleDriveShare::lastShareUrl() const { return m_lastShareUrl; }

void GoogleDriveShare::shareClipUrl(const QUrl &url)
{
    shareClip(url.toLocalFile());
}

void GoogleDriveShare::copyLastShareUrl()
{
    if (m_lastShareUrl.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(m_lastShareUrl);
    setStatusMessage(QStringLiteral("Share link copied to clipboard."));
}

void GoogleDriveShare::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void GoogleDriveShare::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void GoogleDriveShare::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}

void GoogleDriveShare::loadSavedToken()
{
    auto *job = new QKeychain::ReadPasswordJob(m_keychainService, this);
    job->setKey(kKeychainKey);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this](QKeychain::Job *finished) {
        auto *readJob = static_cast<QKeychain::ReadPasswordJob *>(finished);
        if (!readJob->error()) {
            m_refreshToken = readJob->textData();
            setConnected(!m_refreshToken.isEmpty());
            setStatusMessage(m_connected ? QStringLiteral("Google Drive connected · share links are public to anyone who has them.")
                                         : QStringLiteral("A share link lets anyone who receives it view this clip."));
        }
        m_credentialsLoaded = true;
        if (!m_pendingPath.isEmpty()) {
            const QString pendingPath = std::exchange(m_pendingPath, QString());
            setBusy(false);
            shareClip(pendingPath);
        }
    });
    job->start();
}

void GoogleDriveShare::shareClip(const QString &path)
{
    if (m_busy) {
        setStatusMessage(QStringLiteral("A share is already in progress."));
        return;
    }
    const QFileInfo fileInfo(path);
    if (!fileInfo.isFile() || !fileInfo.isReadable()) {
        fail(QStringLiteral("That clip cannot be read from disk."));
        return;
    }
    if (!configured()) {
        fail(QStringLiteral("Google Drive sharing is not enabled in this build yet."));
        return;
    }
    if (!m_lastShareUrl.isEmpty()) {
        m_lastShareUrl.clear();
        emit lastShareUrlChanged();
    }
    if (!m_credentialsLoaded) {
        m_pendingPath = fileInfo.absoluteFilePath();
        setBusy(true);
        setStatusMessage(QStringLiteral("Checking the secure keyring…"));
        loadSavedToken();
        return;
    }
    m_pendingPath = fileInfo.absoluteFilePath();
    setBusy(true);
    if (!m_connected) {
        beginAuthorization();
        return;
    }
    ensureAccessToken([this](bool ok) {
        if (!ok) {
            beginAuthorization();
            return;
        }
        const QString path = std::exchange(m_pendingPath, QString());
        uploadClip(path);
    });
}

void GoogleDriveShare::beginAuthorization()
{
    if (!m_oauthServer.listen(QHostAddress::LocalHost, 0)) {
        fail(QStringLiteral("Could not start the local Google sign-in callback."));
        return;
    }
    m_oauthState = randomUrlSafe(32);
    m_codeVerifier = randomUrlSafe(64);
    const QByteArray challenge = QCryptographicHash::hash(
        m_codeVerifier.toUtf8(), QCryptographicHash::Sha256).toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    m_redirectUrl = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_oauthServer.serverPort()));

    QUrl authorizationUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), kClientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUrl.toString());
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), kDriveFileScope);
    query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("state"), m_oauthState);
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    authorizationUrl.setQuery(query);
    setStatusMessage(QStringLiteral("Approve Moment in your browser. The clip will upload automatically after that."));
    QString browserPath;
    const QString preferredBrowser = qEnvironmentVariable("MOMENT_BROWSER").trimmed();
    if (!preferredBrowser.isEmpty())
        browserPath = QStandardPaths::findExecutable(preferredBrowser);
    if (browserPath.isEmpty()) {
        const QStringList browsers{
            QStringLiteral("google-chrome-stable"), QStringLiteral("google-chrome"),
            QStringLiteral("chromium"), QStringLiteral("chromium-browser"),
            QStringLiteral("firefox"), QStringLiteral("brave-browser"),
            QStringLiteral("microsoft-edge-stable"), QStringLiteral("microsoft-edge")
        };
        for (const QString &browser : browsers) {
            browserPath = QStandardPaths::findExecutable(browser);
            if (!browserPath.isEmpty())
                break;
        }
    }
    const bool browserStarted = !browserPath.isEmpty()
        && QProcess::startDetached(browserPath, {authorizationUrl.toString(QUrl::FullyEncoded)});
    if (!browserStarted) {
        m_oauthServer.close();
        fail(preferredBrowser.isEmpty()
            ? QStringLiteral("Could not find Chrome, Chromium, Firefox, Brave, or Edge. Install one to sign in.")
            : QStringLiteral("The browser selected in MOMENT_BROWSER could not be started."));
    } else {
        qInfo().noquote() << "Moment opened Google sign-in in" << QFileInfo(browserPath).fileName();
    }
    QTimer::singleShot(180000, this, [this] {
        if (m_busy && m_oauthServer.isListening()) {
            m_oauthServer.close();
            fail(QStringLiteral("Google sign-in timed out. Try sharing again."));
        }
    });
}

void GoogleDriveShare::handleAuthorizationRequest()
{
    while (m_oauthServer.hasPendingConnections()) {
        QTcpSocket *socket = m_oauthServer.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            QByteArray request = socket->property("oauthRequest").toByteArray();
            request.append(socket->readAll());
            socket->setProperty("oauthRequest", request);
            if (!request.contains("\r\n\r\n"))
                return;
            const QByteArray requestLine = request.left(request.indexOf("\r\n"));
            const QList<QByteArray> fields = requestLine.split(' ');
            QUrlQuery query;
            if (fields.size() >= 2) {
                const QUrl callbackUrl = QUrl::fromEncoded(fields.at(1));
                query = QUrlQuery(callbackUrl);
            }
            const QString state = query.queryItemValue(QStringLiteral("state"));
            const QString code = query.queryItemValue(QStringLiteral("code"));
            const QString authError = query.queryItemValue(QStringLiteral("error"));
            const bool valid = !m_oauthState.isEmpty() && state == m_oauthState;
            const QString pageTitle = !valid ? QStringLiteral("Sign-in could not be confirmed")
                : !authError.isEmpty() ? QStringLiteral("Sign-in was cancelled")
                : code.isEmpty() ? QStringLiteral("Google did not finish sign-in")
                : QStringLiteral("Back to Moment");
            const QString pageMessage = !valid ? QStringLiteral("This response did not match the sign-in request. Close this page and try sharing again from Moment.")
                : !authError.isEmpty() ? QStringLiteral("Google sign-in was cancelled. Return to Moment and try again when you are ready.")
                : code.isEmpty() ? QStringLiteral("Google did not return an authorization code. Return to Moment and retry the share.")
                : QStringLiteral("Moment received the Google response and is checking access while it uploads your clip. Return to Moment for the final result.");
            const QString accent = !valid || !authError.isEmpty() || code.isEmpty()
                ? QStringLiteral("#ff8a78") : QStringLiteral("#b5ec72");
            const QByteArray responseBody = QStringLiteral(R"HTML(
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark"><title>%1 · Moment</title>
<style>
*{box-sizing:border-box}html,body{margin:0;min-height:100%%;font-family:Inter,"Segoe UI",sans-serif;color:#edf3f6}
body{display:grid;place-items:center;padding:28px;background:radial-gradient(ellipse at 50%% 0,rgba(105,151,80,.18),transparent 48%%),#0c1218}
main{width:min(100%%,480px);padding:34px;border:1px solid #293740;border-radius:22px;background:linear-gradient(145deg,#18232c,#111920);box-shadow:0 28px 90px #0008}
.brand{display:flex;align-items:center;gap:12px;margin-bottom:36px;color:#a9bac4;font-size:12px;letter-spacing:.13em;text-transform:uppercase}
.mark{display:grid;place-items:center;width:42px;height:42px;border-radius:13px;background:#b5ec72;color:#162316;font-size:22px;font-weight:900;letter-spacing:-.08em}
.eyebrow{margin:0 0 11px;color:%2;font-size:11px;font-weight:700;letter-spacing:.14em;text-transform:uppercase}
h1{margin:0 0 13px;font-size:clamp(26px,6vw,34px);letter-spacing:-.04em;line-height:1.1}
.message{margin:0;color:#aebdc6;font-size:15px;line-height:1.65}
.rule{height:1px;margin:27px 0 17px;background:#293740}
.foot{margin:0;color:#718490;font-size:12px;line-height:1.5}
</style></head><body><main>
<div class="brand"><div class="mark">M</div><span>Moment · Google Drive</span></div>
<p class="eyebrow">Secure sign-in</p><h1>%1</h1><p class="message">%3</p>
<div class="rule"></div><p class="foot">You can close this browser page and return to the Moment app.</p>
</main></body></html>
)HTML").arg(pageTitle.toHtmlEscaped(), accent, pageMessage.toHtmlEscaped()).toUtf8();
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                          "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\r\n"
                          "X-Content-Type-Options: nosniff\r\nConnection: close\r\nContent-Length: ");
            socket->write(QByteArray::number(responseBody.size()));
            socket->write("\r\n\r\n");
            socket->write(responseBody);
            socket->disconnectFromHost();
            m_oauthServer.close();
            if (!valid) {
                fail(QStringLiteral("Google sign-in response did not match this request."));
            } else if (!authError.isEmpty()) {
                fail(QStringLiteral("Google sign-in was cancelled."));
            } else if (code.isEmpty()) {
                fail(QStringLiteral("Google did not return an authorization code."));
            } else {
                exchangeAuthorizationCode(code);
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void GoogleDriveShare::exchangeAuthorizationCode(const QString &code)
{
    setStatusMessage(QStringLiteral("Completing Google sign-in…"));
    QNetworkRequest request(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QMap<QString, QString> tokenParameters{
        {QStringLiteral("client_id"), kClientId},
        {QStringLiteral("code"), code},
        {QStringLiteral("code_verifier"), m_codeVerifier},
        {QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
        {QStringLiteral("redirect_uri"), m_redirectUrl.toString()}
    };
    if (!kClientSecret.isEmpty())
        tokenParameters.insert(QStringLiteral("client_secret"), kClientSecret);
    const QByteArray body = formBody(tokenParameters);
    QNetworkReply *reply = m_network.post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray data = reply->readAll();
        const auto json = QJsonDocument::fromJson(data).object();
        if (reply->error() != QNetworkReply::NoError || json.value(QStringLiteral("access_token")).toString().isEmpty()) {
            const QString detail = networkErrorText(reply, json);
            reply->deleteLater();
            fail(QStringLiteral("Google sign-in failed: %1").arg(detail));
            return;
        }
        m_accessToken = json.value(QStringLiteral("access_token")).toString();
        m_accessTokenExpiresAtMs = QDateTime::currentMSecsSinceEpoch()
                                   + json.value(QStringLiteral("expires_in")).toInt(3600) * 1000 - 60000;
        const QString refreshToken = json.value(QStringLiteral("refresh_token")).toString();
        reply->deleteLater();
        if (!refreshToken.isEmpty()) {
            saveRefreshToken(refreshToken, [this](bool saved) {
                if (!saved) {
                    fail(QStringLiteral("Could not save Google sign-in in the system keyring."));
                    return;
                }
                setConnected(true);
                const QString path = std::exchange(m_pendingPath, QString());
                if (!path.isEmpty())
                    uploadClip(path);
            });
        } else if (!m_refreshToken.isEmpty()) {
            setConnected(true);
            const QString path = std::exchange(m_pendingPath, QString());
            if (!path.isEmpty())
                uploadClip(path);
        } else {
            fail(QStringLiteral("Google did not issue offline access. Disconnect Moment in your Google account and retry."));
        }
    });
}

void GoogleDriveShare::refreshAccessToken(std::function<void(bool)> continuation)
{
    if (m_refreshToken.isEmpty()) {
        continuation(false);
        return;
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QMap<QString, QString> tokenParameters{
        {QStringLiteral("client_id"), kClientId},
        {QStringLiteral("refresh_token"), m_refreshToken},
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")}
    };
    if (!kClientSecret.isEmpty())
        tokenParameters.insert(QStringLiteral("client_secret"), kClientSecret);
    QNetworkReply *reply = m_network.post(request, formBody(tokenParameters));
    connect(reply, &QNetworkReply::finished, this, [this, reply, continuation] {
        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError
                        && !json.value(QStringLiteral("access_token")).toString().isEmpty();
        if (ok) {
            m_accessToken = json.value(QStringLiteral("access_token")).toString();
            m_accessTokenExpiresAtMs = QDateTime::currentMSecsSinceEpoch()
                                       + json.value(QStringLiteral("expires_in")).toInt(3600) * 1000 - 60000;
        } else if (json.value(QStringLiteral("error")).toString() == QStringLiteral("invalid_grant")) {
            setConnected(false);
            m_refreshToken.clear();
        }
        reply->deleteLater();
        continuation(ok);
    });
}

void GoogleDriveShare::ensureAccessToken(std::function<void(bool)> continuation)
{
    if (!m_accessToken.isEmpty() && QDateTime::currentMSecsSinceEpoch() < m_accessTokenExpiresAtMs) {
        continuation(true);
        return;
    }
    refreshAccessToken(std::move(continuation));
}

void GoogleDriveShare::saveRefreshToken(const QString &token, std::function<void(bool)> continuation)
{
    auto *job = new QKeychain::WritePasswordJob(m_keychainService, this);
    job->setKey(kKeychainKey);
    job->setTextData(token);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job, token, continuation](QKeychain::Job *finished) {
        const bool ok = !finished->error();
        if (ok)
            m_refreshToken = token;
        job->deleteLater();
        if (continuation)
            continuation(ok);
    });
    job->start();
}

void GoogleDriveShare::uploadClip(const QString &path)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        fail(QStringLiteral("FFmpeg is required to prepare a video for Google Drive playback."));
        return;
    }
    m_shareTempDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/moment-drive-share-XXXXXX"));
    if (!m_shareTempDir->isValid()) {
        m_shareTempDir.reset();
        fail(QStringLiteral("Could not create a temporary folder to prepare the shared video."));
        return;
    }

    m_originalClipName = QFileInfo(path).completeBaseName() + QStringLiteral(".mp4");
    m_transcodeOutputPath = m_shareTempDir->filePath(QStringLiteral("share-ready.mp4"));
    setStatusMessage(QStringLiteral("Preparing a Google Drive-compatible MP4…"));
    m_transcoder.setProgram(ffmpeg);
    m_transcoder.setArguments({
        QStringLiteral("-nostdin"), QStringLiteral("-y"), QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-i"), path,
        QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a?"),
        QStringLiteral("-vf"), QStringLiteral("fps=30,scale=w='min(1920,iw)':h='min(1080,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2,setsar=1"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("veryfast"),
        QStringLiteral("-crf"), QStringLiteral("21"), QStringLiteral("-profile:v"), QStringLiteral("high"),
        QStringLiteral("-level:v"), QStringLiteral("4.1"), QStringLiteral("-g"), QStringLiteral("60"),
        QStringLiteral("-maxrate"), QStringLiteral("12M"), QStringLiteral("-bufsize"), QStringLiteral("24M"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("160k"),
        QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"), QStringLiteral("2"),
        QStringLiteral("-map_metadata"), QStringLiteral("-1"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"), m_transcodeOutputPath
    });
    m_transcoder.setProcessChannelMode(QProcess::SeparateChannels);
    m_transcoder.start();
}

void GoogleDriveShare::finishClipTranscode(int exitCode, QProcess::ExitStatus exitStatus)
{
    const QFileInfo readyInfo(m_transcodeOutputPath);
    if (!m_busy || exitStatus != QProcess::NormalExit || exitCode != 0
        || !readyInfo.isFile() || readyInfo.size() <= 0) {
        m_shareTempDir.reset();
        fail(QStringLiteral("FFmpeg could not prepare this clip for Google Drive playback."));
        return;
    }
    const QString preparedPath = m_transcodeOutputPath;
    m_transcodeOutputPath.clear();
    uploadPreparedClip(preparedPath);
}

void GoogleDriveShare::uploadPreparedClip(const QString &path)
{
    const QFileInfo info(path);
    auto *file = new QFile(info.absoluteFilePath(), this);
    if (!file->open(QIODevice::ReadOnly)) {
        file->deleteLater();
        m_shareTempDir.reset();
        fail(QStringLiteral("Could not open the clip for upload."));
        return;
    }
    const qint64 size = file->size();
    const QString mimeType = QStringLiteral("video/mp4");
    setStatusMessage(QStringLiteral("Uploading %1 (%2 MB)…")
                         .arg(m_originalClipName, QString::number(size / 1048576.0, 'f', 1)));
    QNetworkRequest request(QUrl(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable&fields=id,name,size")));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    request.setRawHeader("X-Upload-Content-Type", mimeType.toUtf8());
    request.setRawHeader("X-Upload-Content-Length", QByteArray::number(size));
    const QJsonObject metadata{
        {QStringLiteral("name"), m_originalClipName},
        {QStringLiteral("mimeType"), mimeType},
        {QStringLiteral("description"), QStringLiteral("Shared from Moment")}
    };
    QNetworkReply *initReply = m_network.post(request, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    connect(initReply, &QNetworkReply::finished, this, [this, initReply, file, size, mimeType] {
        const QUrl uploadUrl = initReply->header(QNetworkRequest::LocationHeader).toUrl();
        if (initReply->error() != QNetworkReply::NoError || !uploadUrl.isValid()) {
            const auto json = QJsonDocument::fromJson(initReply->readAll()).object();
            const QString detail = networkErrorText(initReply, json);
            initReply->deleteLater();
            file->close();
            file->deleteLater();
            fail(QStringLiteral("Could not start the Google Drive upload: %1").arg(detail));
            return;
        }
        initReply->deleteLater();
        QNetworkRequest uploadRequest(uploadUrl);
        uploadRequest.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
        uploadRequest.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
        uploadRequest.setRawHeader("Content-Length", QByteArray::number(size));
        QNetworkReply *uploadReply = m_network.put(uploadRequest, file);
        connect(uploadReply, &QNetworkReply::uploadProgress, this, [this, size](qint64 sent, qint64 total) {
            if (total <= 0) total = size;
            setStatusMessage(QStringLiteral("Uploading clip · %1%")
                                 .arg(total > 0 ? (sent * 100 / total) : 0));
        });
        connect(uploadReply, &QNetworkReply::finished, this, [this, uploadReply, file, size] {
            const auto json = QJsonDocument::fromJson(uploadReply->readAll()).object();
            const bool ok = uploadReply->error() == QNetworkReply::NoError
                            && !json.value(QStringLiteral("id")).toString().isEmpty();
            const QString fileId = json.value(QStringLiteral("id")).toString();
            const QString detail = networkErrorText(uploadReply, json);
            uploadReply->deleteLater();
            file->close();
            file->deleteLater();
            if (!ok) {
                m_shareTempDir.reset();
                fail(QStringLiteral("Google Drive could not finish uploading this clip: %1").arg(detail));
                return;
            }
            bool sizeValid = false;
            const qint64 uploadedSize = json.value(QStringLiteral("size")).toString().toLongLong(&sizeValid);
            if (!sizeValid || uploadedSize != size) {
                cleanupRemoteFile(fileId);
                fail(QStringLiteral("Google Drive did not confirm the complete video upload. Please share the clip again."));
                return;
            }
            m_uploadedFileId = fileId;
            requestSharePermission(fileId);
        });
    });
}

void GoogleDriveShare::requestSharePermission(const QString &fileId)
{
    setStatusMessage(QStringLiteral("Creating a link that anyone with the link can open…"));
    QNetworkRequest request(QUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1/permissions?fields=id")
                                     .arg(QString::fromLatin1(QUrl::toPercentEncoding(fileId)))));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    const QJsonObject permission{
        {QStringLiteral("type"), QStringLiteral("anyone")},
        {QStringLiteral("role"), QStringLiteral("reader")}
    };
    QNetworkReply *reply = m_network.post(request, QJsonDocument(permission).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileId] {
        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString detail = networkErrorText(reply, json);
        reply->deleteLater();
        if (!ok) {
            cleanupRemoteFile(fileId);
            fail(QStringLiteral("The clip uploaded, but Google could not make its share link (%1). The uploaded copy is being removed.").arg(detail));
            return;
        }
        fetchShareLink(fileId);
    });
}

void GoogleDriveShare::fetchShareLink(const QString &fileId)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1?fields=webViewLink")
                                     .arg(QString::fromLatin1(QUrl::toPercentEncoding(fileId)))));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        const QUrl url(json.value(QStringLiteral("webViewLink")).toString());
        const bool ok = reply->error() == QNetworkReply::NoError
                        && url.scheme() == QStringLiteral("https")
                        && url.host() == QStringLiteral("drive.google.com");
        const QString detail = networkErrorText(reply, json);
        reply->deleteLater();
        if (!ok) {
            fail(QStringLiteral("The video uploaded, but its Google Drive link could not be retrieved: %1").arg(detail));
            return;
        }
        m_lastShareUrl = url.toString(QUrl::FullyEncoded);
        emit lastShareUrlChanged();
        QGuiApplication::clipboard()->setText(m_lastShareUrl);
        setBusy(false);
        m_shareTempDir.reset();
        m_transcodeOutputPath.clear();
        m_originalClipName.clear();
        setStatusMessage(QStringLiteral("Google Drive link created and copied. Drive may still be preparing video playback."));
        emit shareCompleted(m_lastShareUrl);
    });
}

void GoogleDriveShare::cleanupRemoteFile(const QString &fileId)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1")
                                     .arg(QString::fromLatin1(QUrl::toPercentEncoding(fileId)))));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
    QNetworkReply *reply = m_network.deleteResource(request);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void GoogleDriveShare::disconnectAccount()
{
    const QString token = m_refreshToken;
    m_accessToken.clear();
    m_refreshToken.clear();
    m_accessTokenExpiresAtMs = 0;
    setConnected(false);
    auto *job = new QKeychain::DeletePasswordJob(m_keychainService, this);
    job->setKey(kKeychainKey);
    job->setInsecureFallback(false);
    job->start();
    if (!token.isEmpty()) {
        QNetworkRequest request(QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        QNetworkReply *reply = m_network.post(request, formBody({{QStringLiteral("token"), token}}));
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    setStatusMessage(QStringLiteral("Google Drive disconnected from Moment."));
}

void GoogleDriveShare::fail(const QString &message)
{
    m_oauthServer.close();
    if (m_transcoder.state() != QProcess::NotRunning) {
        m_transcoder.kill();
        m_transcoder.waitForFinished(1000);
    }
    m_shareTempDir.reset();
    m_transcodeOutputPath.clear();
    m_originalClipName.clear();
    m_pendingPath.clear();
    setBusy(false);
    setStatusMessage(message);
    emit shareFailed(message);
}
