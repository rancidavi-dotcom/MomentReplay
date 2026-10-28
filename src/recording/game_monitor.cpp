#include "game_monitor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QVariant>

#include <algorithm>

namespace {
constexpr auto kProfileSettingsKey = "games/profiles";
constexpr const char *kGamePathMarkers[] = {
    "/steamapps/common/", "/lutris/", "/heroic/", "/games/", "/gog/"
};

QString canonicalProcessName(const QString &name)
{
    return QFileInfo(name.trimmed()).fileName().toCaseFolded();
}

QString launcherDisplayName(const QString &processName, const QString &executablePath,
                            const QString &commandLine)
{
    const QString name = QFileInfo(processName).fileName().toCaseFolded();
    const QString executable = QFileInfo(executablePath).fileName().toCaseFolded();
    const QString arguments = commandLine.toCaseFolded();
    const auto hasName = [&](const QStringList &aliases) {
        return aliases.contains(name) || aliases.contains(executable);
    };

    if (hasName({QStringLiteral("steam"), QStringLiteral("steam.exe"),
                 QStringLiteral("steamwebhelper"), QStringLiteral("steamwebhelper.exe")}))
        return QStringLiteral("Steam");
    if (hasName({QStringLiteral("heroic"), QStringLiteral("heroic.exe"),
                 QStringLiteral("heroic-games-launcher")})
        || arguments.contains(QStringLiteral("com.heroicgameslauncher.hgl")))
        return QStringLiteral("Heroic");
    if (hasName({QStringLiteral("lutris"), QStringLiteral("lutris-wrapper"),
                 QStringLiteral("net.lutris.lutris")})
        || arguments.contains(QStringLiteral("/lutris")))
        return QStringLiteral("Lutris");
    if (hasName({QStringLiteral("galaxyclient.exe"), QStringLiteral("galaxyclientservice.exe"),
                 QStringLiteral("goggalaxy.exe")}))
        return QStringLiteral("GOG Galaxy");
    if (hasName({QStringLiteral("epicgameslauncher.exe"), QStringLiteral("epicgameslauncher"),
                 QStringLiteral("legendary.exe"), QStringLiteral("legendary")}))
        return QStringLiteral("Epic Games");
    if (hasName({QStringLiteral("itch"), QStringLiteral("itch.exe")}))
        return QStringLiteral("itch.io");
    if (hasName({QStringLiteral("battle.net.exe"), QStringLiteral("battle.net"),
                 QStringLiteral("agent.exe")}))
        return QStringLiteral("Battle.net");
    if (hasName({QStringLiteral("eadesktop.exe"), QStringLiteral("ealauncher.exe"),
                 QStringLiteral("eaapp.exe")}))
        return QStringLiteral("EA app");
    if (hasName({QStringLiteral("upc.exe"), QStringLiteral("ubisoftconnect.exe"),
                 QStringLiteral("ubisoftconnect")}))
        return QStringLiteral("Ubisoft Connect");
    if (hasName({QStringLiteral("gamingapp.exe")}))
        return QStringLiteral("Xbox");
    return {};
}
}

GameMonitor::GameMonitor(const QString &settingsFile, QString procRoot, QObject *parent)
    : QAbstractListModel(parent), m_settings(settingsFile, QSettings::IniFormat),
      m_procRoot(std::move(procRoot)),
      m_pollTimer(this)
{
    loadProfiles();
    m_pollTimer.setInterval(1500);
    connect(&m_pollTimer, &QTimer::timeout, this, &GameMonitor::scanProcesses);
    scanProcesses();
    m_pollTimer.start();
}

void GameMonitor::refresh()
{
    scanProcesses();
}

int GameMonitor::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_profiles.size();
}

QVariant GameMonitor::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_profiles.size())
        return {};
    const QJsonObject &profile = m_profiles.at(index.row());
    switch (role) {
    case NameRole: return profile.value(QStringLiteral("name")).toString();
    case ProcessNameRole: return profile.value(QStringLiteral("processName")).toString();
    case AutoRecordRole: return profile.value(QStringLiteral("autoRecord")).toBool();
    case FrameRateRole: return profile.value(QStringLiteral("frameRate")).toInt(30);
    case QualityRole: return profile.value(QStringLiteral("quality")).toInt(3);
    case ReplaySecondsRole: return profile.value(QStringLiteral("replaySeconds")).toInt(30);
    case ReplayClipSecondsRole: return profile.value(QStringLiteral("replayClipSeconds")).toInt(15);
    default: return {};
    }
}

QHash<int, QByteArray> GameMonitor::roleNames() const
{
    return {{NameRole, "gameName"}, {ProcessNameRole, "processName"},
            {AutoRecordRole, "autoRecord"}, {FrameRateRole, "frameRate"},
            {QualityRole, "quality"}, {ReplaySecondsRole, "replaySeconds"},
            {ReplayClipSecondsRole, "replayClipSeconds"}};
}

QString GameMonitor::activeGameName() const
{
    return m_activeGameName;
}

QString GameMonitor::activeProcessName() const
{
    return m_activeProcessName;
}

QStringList GameMonitor::runningGameCandidates() const
{
    return m_runningGameCandidates;
}

QString GameMonitor::presenceTargetName() const
{
    return m_presenceTargetName;
}

int GameMonitor::profileCount() const
{
    return m_profiles.size();
}

bool GameMonitor::addProfile(const QString &processName, const QString &displayName)
{
    const QString normalizedName = QFileInfo(processName.trimmed()).fileName();
    if (normalizedName.isEmpty())
        return false;
    const QString key = canonicalProcessName(normalizedName);
    for (const QJsonObject &profile : std::as_const(m_profiles)) {
        if (canonicalProcessName(profile.value(QStringLiteral("processName")).toString()) == key)
            return false;
    }

    QString inferredName = displayName.trimmed();
    if (inferredName.isEmpty()) {
        for (const ProcessInfo &process : std::as_const(m_processes)) {
            if (canonicalProcessName(process.name) == key) {
                inferredName = process.gameName;
                break;
            }
        }
    }
    if (inferredName.isEmpty())
        inferredName = QFileInfo(normalizedName).completeBaseName();

    beginInsertRows({}, m_profiles.size(), m_profiles.size());
    m_profiles.append(QJsonObject{
        {QStringLiteral("name"), inferredName},
        {QStringLiteral("processName"), normalizedName},
        {QStringLiteral("autoRecord"), false},
        {QStringLiteral("frameRate"), 30},
        {QStringLiteral("quality"), 3},
        {QStringLiteral("replaySeconds"), 30},
        {QStringLiteral("replayClipSeconds"), 15}
    });
    endInsertRows();
    emit profileCountChanged();
    saveProfiles();
    scanProcesses();
    return true;
}

void GameMonitor::removeProfile(int row)
{
    if (row < 0 || row >= m_profiles.size())
        return;
    beginRemoveRows({}, row, row);
    m_profiles.removeAt(row);
    endRemoveRows();
    emit profileCountChanged();
    saveProfiles();
    scanProcesses();
}

void GameMonitor::setProfileAutoRecord(int row, bool enabled)
{
    if (row < 0 || row >= m_profiles.size())
        return;
    if (m_profiles[row].value(QStringLiteral("autoRecord")).toBool() == enabled)
        return;
    m_profiles[row].insert(QStringLiteral("autoRecord"), enabled);
    emit dataChanged(index(row), index(row), {AutoRecordRole});
    saveProfiles();
    if (canonicalProcessName(m_profiles.at(row).value(QStringLiteral("processName")).toString())
        == canonicalProcessName(m_activeProcessName))
        emit activeProfileChanged(profileMap(m_profiles.at(row)));
    scanProcesses();
}

void GameMonitor::setProfileSettings(int row, int frameRate, int quality,
                                     int replaySeconds, int replayClipSeconds)
{
    if (row < 0 || row >= m_profiles.size())
        return;
    frameRate = frameRate == 60 ? 60 : 30;
    quality = qBound(0, quality, 4);
    replaySeconds = qBound(5, replaySeconds, 120);
    if (replayClipSeconds != 15 && replayClipSeconds != 30 && replayClipSeconds != 60
        && replayClipSeconds != 90 && replayClipSeconds != 120)
        replayClipSeconds = 15;

    QJsonObject &profile = m_profiles[row];
    profile.insert(QStringLiteral("frameRate"), frameRate);
    profile.insert(QStringLiteral("quality"), quality);
    profile.insert(QStringLiteral("replaySeconds"), replaySeconds);
    profile.insert(QStringLiteral("replayClipSeconds"), replayClipSeconds);
    emit dataChanged(index(row), index(row), {FrameRateRole, QualityRole,
                                               ReplaySecondsRole, ReplayClipSecondsRole});
    saveProfiles();
    if (row >= 0 && row < m_profiles.size()
        && canonicalProcessName(profile.value(QStringLiteral("processName")).toString())
               == canonicalProcessName(m_activeProcessName))
        emit activeProfileChanged(profileMap(profile));
}

QVariantMap GameMonitor::activeProfileSettings() const
{
    const QString activeKey = canonicalProcessName(m_activeProcessName);
    for (const QJsonObject &profile : m_profiles) {
        if (canonicalProcessName(profile.value(QStringLiteral("processName")).toString()) == activeKey)
            return profileMap(profile);
    }
    return {};
}

void GameMonitor::loadProfiles()
{
    const QJsonDocument document = QJsonDocument::fromJson(m_settings.value(kProfileSettingsKey).toByteArray());
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject())
            continue;
        QJsonObject profile = value.toObject();
        const QString processName = QFileInfo(profile.value(QStringLiteral("processName")).toString()).fileName();
        if (processName.isEmpty())
            continue;
        profile.insert(QStringLiteral("processName"), processName);
        profile.insert(QStringLiteral("name"), profile.value(QStringLiteral("name")).toString(QFileInfo(processName).completeBaseName()));
        profile.insert(QStringLiteral("autoRecord"), profile.value(QStringLiteral("autoRecord")).toBool(false));
        profile.insert(QStringLiteral("frameRate"), profile.value(QStringLiteral("frameRate")).toInt(30) == 60 ? 60 : 30);
        profile.insert(QStringLiteral("quality"), qBound(0, profile.value(QStringLiteral("quality")).toInt(3), 4));
        profile.insert(QStringLiteral("replaySeconds"), qBound(5, profile.value(QStringLiteral("replaySeconds")).toInt(30), 120));
        const int clipSeconds = profile.value(QStringLiteral("replayClipSeconds")).toInt(15);
        profile.insert(QStringLiteral("replayClipSeconds"), (clipSeconds == 15 || clipSeconds == 30 || clipSeconds == 60 || clipSeconds == 90 || clipSeconds == 120) ? clipSeconds : 15);
        m_profiles.append(profile);
    }
}

void GameMonitor::saveProfiles()
{
    QJsonArray array;
    for (const QJsonObject &profile : std::as_const(m_profiles))
        array.append(profile);
    m_settings.setValue(kProfileSettingsKey, QJsonDocument(array).toJson(QJsonDocument::Compact));
    m_settings.sync();
}

void GameMonitor::scanProcesses()
{
    QList<ProcessInfo> processes;
    QStringList candidates;
    QSet<QString> seenCandidates;
    const QDir procDir(m_procRoot);
    const QStringList entries = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : entries) {
        bool numeric = false;
        pid.toLongLong(&numeric);
        if (!numeric)
            continue;
        const QString processDirectory = procDir.filePath(pid);
        const QString executablePath = QFileInfo(processDirectory + QStringLiteral("/exe")).symLinkTarget();
        if (executablePath.isEmpty())
            continue;
        QFile commFile(processDirectory + QStringLiteral("/comm"));
        if (!commFile.open(QIODevice::ReadOnly))
            continue;
        const QString comm = QString::fromLocal8Bit(commFile.readAll()).trimmed();
        const QString executableName = QFileInfo(executablePath).fileName();
        if (executableName.isEmpty() || executableName == QStringLiteral("medal-linux"))
            continue;
        const bool gamePath = looksLikeGame(executablePath);
        const QString processName = comm.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
            ? comm : executableName;
        const QString gameName = gamePath ? cleanGameName(executablePath, executableName)
                                          : QFileInfo(processName).completeBaseName();
        QFile commandLineFile(processDirectory + QStringLiteral("/cmdline"));
        QString commandLine;
        if (commandLineFile.open(QIODevice::ReadOnly)) {
            QByteArray commandBytes = commandLineFile.readAll();
            commandBytes.replace('\0', ' ');
            commandLine = QString::fromLocal8Bit(commandBytes).trimmed();
        }
        const bool gameCandidate = gamePath || comm.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
        processes.append({processName, executablePath, gameName, commandLine, gameCandidate});
        if (gameCandidate) {
            const QString candidateKey = canonicalProcessName(processName);
            if (!seenCandidates.contains(candidateKey)) {
                seenCandidates.insert(candidateKey);
                candidates.append(processName);
            }
        }
    }

    std::sort(processes.begin(), processes.end(), [](const ProcessInfo &a, const ProcessInfo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    std::sort(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    m_processes = processes;
    if (m_runningGameCandidates != candidates) {
        m_runningGameCandidates = candidates;
        emit runningGameCandidatesChanged();
    }

    QJsonObject activeProfile;
    QString activeName;
    QString activeProcess;
    for (const ProcessInfo &process : std::as_const(m_processes)) {
        for (const QJsonObject &profile : std::as_const(m_profiles)) {
            if (canonicalProcessName(profile.value(QStringLiteral("processName")).toString()) == canonicalProcessName(process.name)
                || canonicalProcessName(profile.value(QStringLiteral("processName")).toString()) == canonicalProcessName(QFileInfo(process.executablePath).fileName())) {
                activeProfile = profile;
                activeName = profile.value(QStringLiteral("name")).toString();
                activeProcess = profile.value(QStringLiteral("processName")).toString();
                break;
            }
        }
        if (!activeProcess.isEmpty())
            break;
    }

    if (activeName != m_activeGameName || activeProcess != m_activeProcessName) {
        m_activeGameName = activeName;
        m_activeProcessName = activeProcess;
        emit activeGameChanged();
        emit activeProfileChanged(profileMap(activeProfile));
    }

    QString presenceTarget = m_activeGameName;
    if (presenceTarget.isEmpty()) {
        QMap<QString, QString> detectedGames;
        QSet<QString> launchers;
        for (const ProcessInfo &process : std::as_const(m_processes)) {
            const QString launcher = launcherDisplayName(process.name, process.executablePath,
                                                          process.commandLine);
            if (!launcher.isEmpty()) {
                launchers.insert(launcher);
                continue;
            }
            if (process.gameCandidate) {
                const QString key = process.gameName.toCaseFolded();
                if (!key.isEmpty())
                    detectedGames.insert(key, process.gameName);
            }
        }

        if (detectedGames.size() == 1) {
            presenceTarget = detectedGames.constBegin().value();
        } else if (detectedGames.isEmpty()) {
            const QStringList launcherPriority{
                QStringLiteral("Steam"), QStringLiteral("Heroic"), QStringLiteral("Lutris"),
                QStringLiteral("GOG Galaxy"), QStringLiteral("Epic Games"),
                QStringLiteral("itch.io"), QStringLiteral("Battle.net"),
                QStringLiteral("EA app"), QStringLiteral("Ubisoft Connect"), QStringLiteral("Xbox")
            };
            for (const QString &launcher : launcherPriority) {
                if (launchers.contains(launcher)) {
                    presenceTarget = launcher;
                    break;
                }
            }
        }
    }
    if (presenceTarget != m_presenceTargetName) {
        m_presenceTargetName = presenceTarget;
        emit presenceTargetChanged();
    }
}

QString GameMonitor::cleanGameName(const QString &path, const QString &executable)
{
    const QString lowerPath = path.toCaseFolded();
    const QStringList markers{QStringLiteral("/steamapps/common/"), QStringLiteral("/lutris/"),
                              QStringLiteral("/heroic/"), QStringLiteral("/games/"),
                              QStringLiteral("/gog/")};
    for (const QString &marker : markers) {
        const qsizetype markerIndex = lowerPath.indexOf(marker);
        if (markerIndex < 0)
            continue;
        const QString tail = path.mid(markerIndex + marker.size());
        const QStringList folders = tail.split('/', Qt::SkipEmptyParts);
        for (const QString &folder : folders) {
            const QString key = folder.toCaseFolded();
            if (key == QStringLiteral("heroic") || key == QStringLiteral("lutris")
                || key == QStringLiteral("gog") || key == QStringLiteral("games"))
                continue;
            return folder;
        }
    }
    return QFileInfo(executable).completeBaseName();
}

bool GameMonitor::looksLikeGame(const QString &path)
{
    const QString lowerPath = path.toCaseFolded();
    for (const char *marker : kGamePathMarkers) {
        if (lowerPath.contains(QString::fromLatin1(marker)))
            return true;
    }
    return false;
}

QVariantMap GameMonitor::profileMap(const QJsonObject &profile) const
{
    if (profile.isEmpty())
        return {};
    return {
        {QStringLiteral("name"), profile.value(QStringLiteral("name")).toString()},
        {QStringLiteral("processName"), profile.value(QStringLiteral("processName")).toString()},
        {QStringLiteral("autoRecord"), profile.value(QStringLiteral("autoRecord")).toBool()},
        {QStringLiteral("frameRate"), profile.value(QStringLiteral("frameRate")).toInt(30)},
        {QStringLiteral("quality"), profile.value(QStringLiteral("quality")).toInt(3)},
        {QStringLiteral("replaySeconds"), profile.value(QStringLiteral("replaySeconds")).toInt(30)},
        {QStringLiteral("replayClipSeconds"), profile.value(QStringLiteral("replayClipSeconds")).toInt(15)}
    };
}
