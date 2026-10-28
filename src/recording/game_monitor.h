#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class GameMonitor final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString activeGameName READ activeGameName NOTIFY activeGameChanged)
    Q_PROPERTY(QString activeProcessName READ activeProcessName NOTIFY activeGameChanged)
    Q_PROPERTY(QStringList runningGameCandidates READ runningGameCandidates NOTIFY runningGameCandidatesChanged)
    Q_PROPERTY(QString presenceTargetName READ presenceTargetName NOTIFY presenceTargetChanged)
    Q_PROPERTY(int profileCount READ profileCount NOTIFY profileCountChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        ProcessNameRole,
        AutoRecordRole,
        FrameRateRole,
        QualityRole,
        ReplaySecondsRole,
        ReplayClipSecondsRole
    };
    Q_ENUM(Role)

    explicit GameMonitor(const QString &settingsFile, QString procRoot = QStringLiteral("/proc"),
                         QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeGameName() const;
    QString activeProcessName() const;
    QStringList runningGameCandidates() const;
    QString presenceTargetName() const;
    int profileCount() const;

    Q_INVOKABLE bool addProfile(const QString &processName, const QString &displayName = {});
    Q_INVOKABLE void removeProfile(int row);
    Q_INVOKABLE void setProfileAutoRecord(int row, bool enabled);
    Q_INVOKABLE void setProfileSettings(int row, int frameRate, int quality,
                                         int replaySeconds, int replayClipSeconds);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap activeProfileSettings() const;

signals:
    void activeGameChanged();
    void activeProfileChanged(const QVariantMap &profile);
    void runningGameCandidatesChanged();
    void presenceTargetChanged();
    void profileCountChanged();

private:
    struct ProcessInfo {
        QString name;
        QString executablePath;
        QString gameName;
        QString commandLine;
        bool gameCandidate = false;
    };

    void loadProfiles();
    void saveProfiles();
    void scanProcesses();
    static QString cleanGameName(const QString &path, const QString &executable);
    static bool looksLikeGame(const QString &path);
    QVariantMap profileMap(const QJsonObject &profile) const;

    QSettings m_settings;
    QString m_procRoot;
    QTimer m_pollTimer;
    QList<QJsonObject> m_profiles;
    QStringList m_runningGameCandidates;
    QList<ProcessInfo> m_processes;
    QString m_activeGameName;
    QString m_activeProcessName;
    QString m_presenceTargetName;
};
