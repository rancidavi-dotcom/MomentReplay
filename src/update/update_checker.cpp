#include "update_checker.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>
#include <QVersionNumber>

namespace {
const QUrl kLatestReleaseApi(QStringLiteral(
    "https://api.github.com/repos/rancidavi-dotcom/MomentReplay/releases/latest"));
const QString kReleasePage = QStringLiteral("https://github.com/rancidavi-dotcom/MomentReplay/releases");
constexpr qint64 kCheckIntervalSeconds = 24 * 60 * 60;

bool isLoopbackHost(const QString &host)
{
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    QHostAddress address;
    return address.setAddress(host) && address.isLoopback();
}

QVersionNumber versionFromTag(QString tag)
{
    if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        tag.remove(0, 1);
    return QVersionNumber::fromString(tag);
}
}

UpdateChecker::UpdateChecker(const QString &settingsFile, const QString &currentVersion,
                             bool enabled, QObject *parent,
                             const QUrl &latestReleaseApiForTest,
                             const QString &installPathForTest)
    : QObject(parent), m_settings(settingsFile, QSettings::IniFormat),
      m_currentVersion(currentVersion), m_enabled(enabled)
{
    const bool testApiIsLocal = isLoopbackHost(latestReleaseApiForTest.host());
    const bool testApiIsGithub = latestReleaseApiForTest.scheme() == QStringLiteral("https")
        && latestReleaseApiForTest.host() == QStringLiteral("api.github.com");
    m_testEndpoints = !installPathForTest.isEmpty() && !latestReleaseApiForTest.isEmpty()
        && (testApiIsLocal || testApiIsGithub);
    if (m_testEndpoints)
        m_latestReleaseApi = latestReleaseApiForTest;
    else
        m_latestReleaseApi = kLatestReleaseApi;
    m_installPathForTest = m_testEndpoints ? installPathForTest : QString();
    m_autoCheck = m_settings.value(QStringLiteral("updates/autoCheck"), true).toBool();
    m_periodicTimer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_periodicTimer, &QTimer::timeout, this, &UpdateChecker::checkIfDue);
    if (m_enabled && m_autoCheck) {
        m_periodicTimer.start();
        QTimer::singleShot(4000, this, &UpdateChecker::checkIfDue);
    }
}

bool UpdateChecker::autoCheck() const { return m_autoCheck; }
bool UpdateChecker::checking() const { return m_checking; }
bool UpdateChecker::updateAvailable() const { return m_updateAvailable; }
bool UpdateChecker::canInstallUpdate() const { return m_canInstallUpdate; }
QString UpdateChecker::currentVersion() const { return m_currentVersion; }
QString UpdateChecker::latestVersion() const { return m_latestVersion; }
QString UpdateChecker::status() const { return m_status; }
bool UpdateChecker::installSupported() const { return m_installSupported; }
bool UpdateChecker::downloading() const { return m_downloading; }
int UpdateChecker::downloadPercent() const { return m_downloadPercent; }
bool UpdateChecker::restartRequired() const { return m_restartRequired; }

void UpdateChecker::refreshInstallSupport()
{
    if (!m_installPathForTest.isEmpty()) {
        m_installSupported = QFileInfo(QFileInfo(m_installPathForTest).absolutePath()).isWritable();
        return;
    }
#ifdef Q_OS_WIN
    const QFileInfo executable(QCoreApplication::applicationFilePath());
    m_installSupported = executable.fileName().compare(QStringLiteral("Moment.exe"), Qt::CaseInsensitive) == 0
        && QFileInfo(executable.absolutePath()).isWritable();
#elif defined(Q_OS_LINUX)
    const QString target = QDir::home().filePath(QStringLiteral(".local/bin/medal-linux"));
    m_installSupported = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath()
            == QFileInfo(target).absoluteFilePath()
        && QFileInfo(QFileInfo(target).absolutePath()).isWritable();
#else
    m_installSupported = false;
#endif
}

void UpdateChecker::setAutoCheck(bool enabled)
{
    if (enabled == m_autoCheck)
        return;
    m_autoCheck = enabled;
    m_settings.setValue(QStringLiteral("updates/autoCheck"), enabled);
    m_settings.sync();
    if (enabled && m_enabled) {
        m_periodicTimer.start();
        QTimer::singleShot(0, this, &UpdateChecker::checkIfDue);
    } else {
        m_periodicTimer.stop();
    }
    emit autoCheckChanged();
}

void UpdateChecker::checkNow()
{
    if (!m_enabled || m_checking)
        return;

    QNetworkRequest request(m_latestReleaseApi);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "Moment-Desktop-Update-Check");
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_checking = true;
    m_status = QStringLiteral("Checking for updates…");
    emit stateChanged();
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        handleReply(reply);
        reply->deleteLater();
    });
}

void UpdateChecker::openReleasePage()
{
    const QUrl url(m_releaseUrl.isEmpty() ? kReleasePage : m_releaseUrl);
    if (url.scheme() == QStringLiteral("https")
        && url.host() == QStringLiteral("github.com"))
        QDesktopServices::openUrl(url);
}

void UpdateChecker::downloadAndInstall()
{
    if (m_checking || m_downloading || !m_updateAvailable)
        return;
    refreshInstallSupport();
    if (!m_installSupported || m_assetUrl.isEmpty() || !m_assetDigest.startsWith(QStringLiteral("sha256:"))) {
        m_status = QStringLiteral("Automatic installation is not supported for this installation");
        emit stateChanged();
        return;
    }

    const QUrl assetUrl(m_assetUrl);
    const bool secureReleaseHost = assetUrl.scheme() == QStringLiteral("https")
        && assetUrl.host() == QStringLiteral("github.com");
    const bool localTestHost = m_testEndpoints && assetUrl.scheme() == QStringLiteral("http")
        && isLoopbackHost(assetUrl.host());
    if (!secureReleaseHost && !localTestHost) {
        m_status = QStringLiteral("Update package URL is invalid");
        emit stateChanged();
        return;
    }

    QString downloadName = QStringLiteral("MomentUpdate-%1")
                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
#ifdef Q_OS_WIN
    downloadName += QStringLiteral(".exe");
#endif
    m_downloadPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                         .filePath(downloadName);
    m_downloadFile.setFileName(m_downloadPath);
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_status = QStringLiteral("Could not create the update download file");
        emit stateChanged();
        return;
    }
    m_downloadHash.reset();
    m_downloadPercent = 0;
    m_downloading = true;
    m_status = QStringLiteral("Downloading update…");
    emit stateChanged();

    QNetworkRequest request(assetUrl);
    request.setRawHeader("Accept", "application/octet-stream");
    request.setRawHeader("User-Agent", "Moment-Desktop-Updater");
    request.setTransferTimeout(120000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_downloadReply = m_network.get(request);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
        if (!m_downloadReply)
            return;
        const QByteArray data = m_downloadReply->readAll();
        if (m_downloadFile.write(data) != data.size()) {
            m_downloadReply->abort();
            return;
        }
        m_downloadHash.addData(data);
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        m_downloadPercent = total > 0 ? int((received * 100) / total) : 0;
        emit stateChanged();
    });
    connect(m_downloadReply, &QNetworkReply::finished, this, [this] {
        if (!m_downloadReply)
            return;
        auto *reply = m_downloadReply.data();
        handleAssetReply(reply);
        reply->deleteLater();
    });
}

void UpdateChecker::handleAssetReply(QNetworkReply *reply)
{
    const QByteArray tail = reply->readAll();
    const bool wroteTail = m_downloadFile.write(tail) == tail.size();
    if (wroteTail)
        m_downloadHash.addData(tail);
    const bool flushed = m_downloadFile.flush();
    m_downloadFile.close();

    const QUrl finalUrl = reply->url();
    const QString expectedDigest = m_assetDigest.mid(QStringLiteral("sha256:").size()).toLower();
    const QString actualDigest = QString::fromLatin1(m_downloadHash.result().toHex());
    if (reply->error() != QNetworkReply::NoError || !wroteTail || !flushed
        || (finalUrl.scheme() != QStringLiteral("https")
            && !(m_testEndpoints && finalUrl.scheme() == QStringLiteral("http")
                 && isLoopbackHost(finalUrl.host())))
        || (actualDigest != expectedDigest)) {
        QFile::remove(m_downloadPath);
        m_downloading = false;
        m_status = reply->error() == QNetworkReply::NoError && wroteTail && flushed
            ? QStringLiteral("Downloaded update failed its SHA-256 check")
            : QStringLiteral("Could not download the update");
        m_downloadReply.clear();
        emit stateChanged();
        return;
    }

#ifdef Q_OS_WIN
    const QString scriptPath = m_downloadPath + QStringLiteral(".ps1");
    QFile script(scriptPath);
    const QString installDir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    const QString scriptText = QStringLiteral(
        "param([int]$ProcessId,[string]$Setup,[string]$InstallDir)\n"
        "$ErrorActionPreference='Stop'\n"
        "while (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 300 }\n"
        "try {\n"
        "  $dirArg='/DIR=\"' + $InstallDir + '\"'\n"
        "  $result=Start-Process -FilePath $Setup -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',$dirArg) -Wait -PassThru\n"
        "  if ($result.ExitCode -ne 0) { throw ('Moment installer failed with exit code ' + $result.ExitCode) }\n"
        "  if (!(Test-Path (Join-Path $InstallDir 'Moment.exe'))) { throw 'Moment.exe is missing after installation' }\n"
        "  Start-Process -FilePath (Join-Path $InstallDir 'Moment.exe')\n"
        "} catch {\n"
        "  exit 1\n"
        "} finally { Remove-Item -LiteralPath $Setup -Force -ErrorAction SilentlyContinue; Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue }\n");
    bool scheduled = false;
    if (script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        script.write(scriptText.toUtf8());
        script.close();
        QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
        if (!powershell.isEmpty()) {
            scheduled = QProcess::startDetached(powershell,
                {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                 QStringLiteral("-File"), scriptPath, QString::number(QCoreApplication::applicationPid()),
                 m_downloadPath, installDir});
        }
    }
    if (!scheduled) {
        QFile::remove(scriptPath);
        QFile::remove(m_downloadPath);
        m_downloading = false;
        m_status = QStringLiteral("Could not start the Windows update helper");
        m_downloadReply.clear();
        emit stateChanged();
        return;
    }
    m_status = QStringLiteral("Update verified. Moment will restart to install it.");
    m_restartRequired = true;
    m_updateAvailable = false;
    m_canInstallUpdate = false;
#elif defined(Q_OS_LINUX)
    const QString target = m_installPathForTest.isEmpty()
        ? QDir::home().filePath(QStringLiteral(".local/bin/medal-linux")) : m_installPathForTest;
    QFile source(m_downloadPath);
    QSaveFile destination(target);
    destination.setDirectWriteFallback(false);
    bool installed = source.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly);
    if (installed) {
        const QFile::Permissions permissions = QFile::permissions(target);
        destination.setPermissions(permissions);
        while (!source.atEnd()) {
            const QByteArray data = source.read(1024 * 1024);
            if (data.isEmpty() && source.error() != QFile::NoError) {
                installed = false;
                break;
            }
            if (destination.write(data) != data.size()) {
                installed = false;
                break;
            }
        }
    }
    if (installed)
        installed = destination.commit();
    else
        destination.cancelWriting();
    QFile::remove(m_downloadPath);
    if (!installed) {
        m_downloading = false;
        m_status = QStringLiteral("Could not install the update in ~/.local/bin");
        m_downloadReply.clear();
        emit stateChanged();
        return;
    }
    m_status = QStringLiteral("Update installed. Restart Moment to use it.");
    m_restartRequired = true;
    m_updateAvailable = false;
    m_canInstallUpdate = false;
#endif
    m_downloading = false;
    m_downloadPercent = 100;
    m_downloadReply.clear();
    emit stateChanged();
}

void UpdateChecker::restartToApply()
{
    if (!m_restartRequired)
        return;
#ifdef Q_OS_WIN
    QCoreApplication::quit();
#elif defined(Q_OS_LINUX)
    const QString executable = QCoreApplication::applicationFilePath();
    const QString scriptPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("moment-restart-%1.sh")
                                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile script(scriptPath);
    const QByteArray scriptBody =
        "#!/bin/sh\n"
        "while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done\n"
        "pid=$1; shift\n"
        "rm -f -- \"$0\"\n"
        "exec \"$@\"\n";
    if (!script.open(QIODevice::WriteOnly) || script.write(scriptBody) != scriptBody.size()) {
        QFile::remove(scriptPath);
        return;
    }
    script.close();
    if (QProcess::startDetached(QStringLiteral("/bin/sh"),
            {scriptPath, QString::number(QCoreApplication::applicationPid()), executable},
            QFileInfo(executable).absolutePath()))
        QCoreApplication::quit();
    else
        QFile::remove(scriptPath);
#endif
}

void UpdateChecker::checkIfDue()
{
    if (!m_enabled || !m_autoCheck || m_checking)
        return;
    const QDateTime lastCheck = QDateTime::fromString(
        m_settings.value(QStringLiteral("updates/lastCheckUtc")).toString(), Qt::ISODate);
    if (lastCheck.isValid()
        && lastCheck.secsTo(QDateTime::currentDateTimeUtc()) < kCheckIntervalSeconds)
        return;
    checkNow();
}

void UpdateChecker::handleReply(QNetworkReply *reply)
{
    m_checking = false;
    const auto recordSuccessfulCheck = [this] {
        m_settings.setValue(QStringLiteral("updates/lastCheckUtc"),
                             QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        m_settings.sync();
    };

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus == 404) {
        recordSuccessfulCheck();
        m_updateAvailable = false;
        m_canInstallUpdate = false;
        m_latestVersion.clear();
        m_releaseUrl = kReleasePage;
        m_status = QStringLiteral("No stable release published yet");
        emit stateChanged();
        return;
    }
    if (reply->error() != QNetworkReply::NoError || httpStatus < 200 || httpStatus >= 300) {
        m_status = QStringLiteral("Could not check for updates");
        emit stateChanged();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_status = QStringLiteral("Could not read update information");
        emit stateChanged();
        return;
    }

    const QJsonObject release = document.object();
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    const QVersionNumber latest = versionFromTag(tag);
    const QVersionNumber current = versionFromTag(m_currentVersion);
    const QUrl releaseUrl(release.value(QStringLiteral("html_url")).toString());
    if (latest.isNull() || current.isNull() || releaseUrl.scheme() != QStringLiteral("https")
        || releaseUrl.host() != QStringLiteral("github.com")) {
        m_status = QStringLiteral("Could not read update information");
        emit stateChanged();
        return;
    }

#ifdef Q_OS_WIN
    const QString expectedAsset = QStringLiteral("Moment-Setup-x64.exe");
#elif defined(Q_OS_LINUX)
    const QString expectedAsset = QStringLiteral("Moment-Linux-x86_64");
#else
    const QString expectedAsset;
#endif
    QString assetUrl;
    QString assetDigest;
    for (const QJsonValue &assetValue : release.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = assetValue.toObject();
        if (asset.value(QStringLiteral("name")).toString() != expectedAsset)
            continue;
        const QUrl candidateUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        const bool secureAsset = candidateUrl.scheme() == QStringLiteral("https")
            && candidateUrl.host() == QStringLiteral("github.com");
        const bool localTestAsset = m_testEndpoints && candidateUrl.scheme() == QStringLiteral("http")
            && isLoopbackHost(candidateUrl.host());
        if (!secureAsset && !localTestAsset)
            continue;
        assetUrl = candidateUrl.toString();
        assetDigest = asset.value(QStringLiteral("digest")).toString();
        break;
    }

    recordSuccessfulCheck();
    m_latestVersion = tag;
    m_releaseUrl = releaseUrl.toString();
    m_assetUrl = assetUrl;
    m_assetDigest = assetDigest;
    m_updateAvailable = QVersionNumber::compare(latest, current) > 0;
    refreshInstallSupport();
    m_canInstallUpdate = m_updateAvailable && m_installSupported
        && !m_assetUrl.isEmpty() && m_assetDigest.startsWith(QStringLiteral("sha256:"));
    if (!m_updateAvailable)
        m_status = QStringLiteral("Moment is up to date");
    else if (m_assetUrl.isEmpty() || !m_assetDigest.startsWith(QStringLiteral("sha256:")))
        m_status = QStringLiteral("Update is available, but its verified package is missing");
    else if (!m_installSupported)
        m_status = QStringLiteral("Update available; install through your package source");
    else
        m_status = QStringLiteral("Update available");
    emit stateChanged();
}
