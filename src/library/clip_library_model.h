#pragma once

#include "clip_repository.h"

#include <QAbstractListModel>
#include <QUrl>

class ClipLibraryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(int clipCount READ clipCount NOTIFY clipCountChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    enum Role {
        ClipIdRole = Qt::UserRole + 1,
        PathRole,
        TitleRole,
        SizeBytesRole,
        ImportedAtRole,
        FavoriteRole,
        AvailableRole,
        SourceUrlRole,
        ImageRole
    };
    Q_ENUM(Role)

    explicit ClipLibraryModel(ClipRepository &repository, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString searchQuery() const;
    void setSearchQuery(const QString &query);
    int clipCount() const;
    QString statusMessage() const;

    Q_INVOKABLE void importFile(const QString &path);
    Q_INVOKABLE void removeFromLibrary(qint64 id);
    Q_INVOKABLE void toggleFavorite(qint64 id);
    Q_INVOKABLE void reload();

signals:
    void searchQueryChanged();
    void clipCountChanged();
    void statusMessageChanged();
    void operationFailed(const QString &message);

private:
    void setStatusMessage(const QString &message);

    ClipRepository &m_repository;
    QList<ClipRecord> m_records;
    QString m_searchQuery;
    QString m_statusMessage = QStringLiteral("Ready");
};
