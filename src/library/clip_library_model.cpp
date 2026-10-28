#include "clip_library_model.h"

#include <QFileInfo>

ClipLibraryModel::ClipLibraryModel(ClipRepository &repository, QObject *parent)
    : QAbstractListModel(parent), m_repository(repository)
{
    reload();
}

int ClipLibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_records.size();
}

QVariant ClipLibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size())
        return {};

    const ClipRecord &record = m_records.at(index.row());
    switch (role) {
    case ClipIdRole: return record.id;
    case PathRole: return record.path;
    case TitleRole: return record.title;
    case SizeBytesRole: return record.sizeBytes;
    case ImportedAtRole: return record.importedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    case FavoriteRole: return record.favorite;
    case AvailableRole: return record.available;
    case SourceUrlRole: return QUrl::fromLocalFile(record.path);
    case ImageRole: {
        const QString suffix = QFileInfo(record.path).suffix().toLower();
        return suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg")
            || suffix == QStringLiteral("jpeg");
    }
    case Qt::DisplayRole: return record.title;
    default: return {};
    }
}

QHash<int, QByteArray> ClipLibraryModel::roleNames() const
{
    return {
        {ClipIdRole, "clipId"},
        {PathRole, "path"},
        {TitleRole, "title"},
        {SizeBytesRole, "sizeBytes"},
        {ImportedAtRole, "importedAt"},
        {FavoriteRole, "favorite"},
        {AvailableRole, "available"},
        {SourceUrlRole, "sourceUrl"},
        {ImageRole, "isImage"}
    };
}

QString ClipLibraryModel::searchQuery() const
{
    return m_searchQuery;
}

void ClipLibraryModel::setSearchQuery(const QString &query)
{
    if (m_searchQuery == query)
        return;
    m_searchQuery = query;
    emit searchQueryChanged();
    reload();
}

int ClipLibraryModel::clipCount() const
{
    return m_records.size();
}

QString ClipLibraryModel::statusMessage() const
{
    return m_statusMessage;
}

void ClipLibraryModel::importFile(const QString &path)
{
    QString error;
    const qint64 id = m_repository.importFile(path, &error);
    if (id == 0) {
        emit operationFailed(error);
        setStatusMessage(QStringLiteral("Import failed"));
        return;
    }

    reload();
    setStatusMessage(QStringLiteral("Clip added to this library"));
}

void ClipLibraryModel::removeFromLibrary(qint64 id)
{
    QString error;
    if (!m_repository.removeFromLibrary(id, &error)) {
        emit operationFailed(error);
        return;
    }
    reload();
    setStatusMessage(QStringLiteral("Removed from library; media file was kept"));
}

void ClipLibraryModel::toggleFavorite(qint64 id)
{
    const auto it = std::find_if(m_records.cbegin(), m_records.cend(), [id](const ClipRecord &record) {
        return record.id == id;
    });
    if (it == m_records.cend())
        return;

    QString error;
    if (!m_repository.setFavorite(id, !it->favorite, &error)) {
        emit operationFailed(error);
        return;
    }
    reload();
}

void ClipLibraryModel::reload()
{
    beginResetModel();
    m_records = m_repository.list(m_searchQuery);
    endResetModel();
    emit clipCountChanged();
}

void ClipLibraryModel::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}
