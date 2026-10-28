#include "video_editor.h"

#include "../library/clip_library_model.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {

QString timeArgument(qint64 milliseconds)
{
    return QString::number(static_cast<double>(milliseconds) / 1000.0, 'f', 3);
}

} // namespace

VideoEditor::~VideoEditor()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    if (!m_partialPath.isEmpty())
        QFile::remove(m_partialPath);
}

VideoEditor::VideoEditor(ClipLibraryModel &library, QString projectsDirectory,
                         QString exportDirectory, QObject *parent)
    : QObject(parent),
      m_library(library),
      m_projectsDirectory(std::move(projectsDirectory)),
      m_exportDirectory(std::move(exportDirectory))
{
    m_projectSaveTimer.setSingleShot(true);
    m_projectSaveTimer.setInterval(400);
    connect(&m_projectSaveTimer, &QTimer::timeout, this, [this] { saveProject(); });
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &VideoEditor::handleProcessOutput);
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &VideoEditor::finishExport);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_exporting)
            return;
        QFile::remove(m_partialPath);
        setExporting(false);
        setExportProgress(0);
        const QString message = QStringLiteral("FFmpeg could not start. Install FFmpeg to export edited clips.");
        setStatusMessage(message);
        emit exportFailed(message);
    });
}

QString VideoEditor::sourcePath() const { return m_sourcePath; }
QUrl VideoEditor::sourceUrl() const { return m_sourcePath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_sourcePath); }
qint64 VideoEditor::durationMs() const { return m_durationMs; }
qint64 VideoEditor::inMs() const { return m_inMs; }
qint64 VideoEditor::outMs() const { return m_outMs; }
bool VideoEditor::dirty() const { return m_dirty; }
bool VideoEditor::exporting() const { return m_exporting; }
int VideoEditor::exportProgress() const { return m_exportProgress; }
QString VideoEditor::statusMessage() const { return m_statusMessage; }
QString VideoEditor::projectPath() const { return m_projectPath; }
int VideoEditor::exportPreset() const { return m_exportPreset; }

QString VideoEditor::exportPresetName() const
{
    static const QStringList names = {
        QStringLiteral("Compact"), QStringLiteral("Balanced"),
        QStringLiteral("High quality"), QStringLiteral("Maximum quality")
    };
    return names.value(m_exportPreset);
}

QString VideoEditor::exportPresetDescription() const
{
    static const QStringList descriptions = {
        QStringLiteral("Smallest file · more visible compression · AAC 96 kb/s"),
        QStringLiteral("Balanced size and detail · AAC 128 kb/s"),
        QStringLiteral("More detail · larger file · AAC 192 kb/s"),
        QStringLiteral("Highest detail · largest file · AAC 256 kb/s")
    };
    return descriptions.value(m_exportPreset);
}

void VideoEditor::setExportPreset(int preset)
{
    const int normalized = qBound(0, preset, 3);
    if (m_exportPreset == normalized)
        return;
    m_exportPreset = normalized;
    emit exportPresetChanged();
}

void VideoEditor::setDurationMs(qint64 duration)
{
    const qint64 normalized = qMax<qint64>(0, duration);
    if (normalized == 0)
        return;
    if (m_durationMs == normalized)
        return;
    m_durationMs = normalized;
    if (m_outMs == 0 && m_durationMs > 0)
        m_outMs = m_durationMs;
    m_inMs = qBound<qint64>(0, m_inMs, m_durationMs);
    m_outMs = qBound<qint64>(m_inMs, m_outMs, m_durationMs);
    emit durationChanged();
    emit rangeChanged();
}

void VideoEditor::setInMs(qint64 position)
{
    const qint64 normalized = qBound<qint64>(0, position, m_durationMs);
    if (m_inMs == normalized)
        return;
    m_inMs = normalized;
    if (m_outMs < m_inMs)
        m_outMs = m_inMs;
    emit rangeChanged();
    setDirty(true);
    m_projectSaveTimer.start();
}

void VideoEditor::setOutMs(qint64 position)
{
    const qint64 normalized = qBound<qint64>(m_inMs, position, m_durationMs);
    if (m_outMs == normalized)
        return;
    m_outMs = normalized;
    emit rangeChanged();
    setDirty(true);
    m_projectSaveTimer.start();
}

void VideoEditor::openClip(const QString &path)
{
    if (m_exporting)
        cancelExport();

    if (path.trimmed().isEmpty()) {
        m_sourcePath.clear();
        m_projectPath.clear();
        m_durationMs = 0;
        m_inMs = 0;
        m_outMs = 0;
        setDirty(false);
        setStatusMessage(QStringLiteral("Choose a video to begin editing."));
        emit sourceChanged();
        emit durationChanged();
        emit rangeChanged();
        emit projectPathChanged();
        return;
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        m_sourcePath.clear();
        m_projectPath.clear();
        m_durationMs = 0;
        m_inMs = 0;
        m_outMs = 0;
        setDirty(false);
        setStatusMessage(QStringLiteral("This video is no longer available."));
        emit sourceChanged();
        emit durationChanged();
        emit rangeChanged();
        emit projectPathChanged();
        return;
    }

    const QString canonicalPath = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath() : info.canonicalFilePath();
    if (m_sourcePath == canonicalPath)
        return;

    m_sourcePath = canonicalPath;
    m_durationMs = 0;
    m_inMs = 0;
    m_outMs = 0;
    m_projectPath = projectPathForSource(m_sourcePath);
    setDirty(true);
    loadProject();
    setStatusMessage(m_dirty ? QStringLiteral("Set the trim points, then save the edit project.")
                             : QStringLiteral("Edit project restored."));
    emit sourceChanged();
    emit durationChanged();
    emit rangeChanged();
    emit projectPathChanged();
}

bool VideoEditor::saveProject()
{
    const QFileInfo mediaInfo(m_sourcePath);
    if (m_sourcePath.isEmpty() || !mediaInfo.exists() || m_durationMs <= 0
        || m_inMs < 0 || m_outMs <= m_inMs || m_outMs > m_durationMs) {
        setStatusMessage(QStringLiteral("Load a valid video and choose a non-empty trim range first."));
        return false;
    }
    if (!QDir().mkpath(m_projectsDirectory)) {
        setStatusMessage(QStringLiteral("Could not create the local editor projects folder."));
        return false;
    }

    QJsonObject project;
    project.insert(QStringLiteral("schemaVersion"), 1);
    project.insert(QStringLiteral("sourcePath"), m_sourcePath);
    project.insert(QStringLiteral("sourceSizeBytes"), mediaInfo.size());
    project.insert(QStringLiteral("sourceModifiedUtc"), mediaInfo.lastModified().toUTC().toString(Qt::ISODateWithMs));
    project.insert(QStringLiteral("durationMs"), m_durationMs);
    project.insert(QStringLiteral("inMs"), m_inMs);
    project.insert(QStringLiteral("outMs"), m_outMs);

    QSaveFile file(m_projectPath);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(project).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        setStatusMessage(QStringLiteral("Could not save the edit project: %1").arg(file.errorString()));
        return false;
    }
    setDirty(false);
    setStatusMessage(QStringLiteral("Edit project saved locally."));
    return true;
}

bool VideoEditor::exportClip()
{
    const QFileInfo mediaInfo(m_sourcePath);
    if (m_exporting || m_sourcePath.isEmpty() || !mediaInfo.exists()
        || m_durationMs <= 0 || m_inMs < 0 || m_outMs <= m_inMs
        || m_outMs > m_durationMs) {
        setStatusMessage(QStringLiteral("Choose a valid video and trim range before exporting."));
        return false;
    }
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        const QString message = QStringLiteral("FFmpeg is required to export edited clips.");
        setStatusMessage(message);
        emit exportFailed(message);
        return false;
    }
    if (!QDir().mkpath(m_exportDirectory)) {
        const QString message = QStringLiteral("Could not create the Moment exports folder.");
        setStatusMessage(message);
        emit exportFailed(message);
        return false;
    }

    m_projectSaveTimer.stop();
    saveProject();

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"));
    const QString baseName = QFileInfo(m_sourcePath).completeBaseName();
    m_finalPath = QDir(m_exportDirectory).filePath(baseName + QStringLiteral("-edit-") + stamp
                                                   + QStringLiteral("-")
                                                   + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)
                                                   + QStringLiteral(".mp4"));
    m_partialPath = QDir(m_exportDirectory).filePath(
        QStringLiteral(".") + baseName + QStringLiteral("-") + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QStringLiteral(".partial.mp4"));
    m_cancelRequested = false;
    m_stdoutBuffer.clear();
    setExportProgress(0);
    setExporting(true);
    setStatusMessage(QStringLiteral("Exporting %1 quality…").arg(exportPresetName()));

    const QString selectedDuration = timeArgument(m_outMs - m_inMs);
    static const int videoCrf[] = {30, 24, 19, 16};
    static const int audioBitrate[] = {96, 128, 192, 256};
    QStringList arguments = {
        QStringLiteral("-nostdin"), QStringLiteral("-y"), QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-progress"),
        QStringLiteral("pipe:1"), QStringLiteral("-i"), m_sourcePath,
        QStringLiteral("-ss"), timeArgument(m_inMs), QStringLiteral("-t"), selectedDuration,
        QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a?"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("medium"),
        QStringLiteral("-crf"), QString::number(videoCrf[m_exportPreset]),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-profile:v"), QStringLiteral("high"),
        QStringLiteral("-c:a"), QStringLiteral("aac"),
        QStringLiteral("-b:a"), QString::number(audioBitrate[m_exportPreset]) + QStringLiteral("k"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        m_partialPath
    };
    m_process.setProgram(ffmpeg);
    m_process.setArguments(arguments);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
    return true;
}

void VideoEditor::cancelExport()
{
    if (!m_exporting)
        return;
    m_cancelRequested = true;
    setStatusMessage(QStringLiteral("Canceling export…"));
    m_process.terminate();
    QTimer::singleShot(1500, this, [this] {
        if (m_process.state() != QProcess::NotRunning)
            m_process.kill();
    });
}

QString VideoEditor::projectPathForSource(const QString &path) const
{
    const QByteArray key = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    return QDir(m_projectsDirectory).filePath(QString::fromLatin1(key) + QStringLiteral(".momentedit"));
}

void VideoEditor::loadProject()
{
    QFile file(m_projectPath);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        setStatusMessage(QStringLiteral("The saved edit project could not be read."));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject project = document.object();
    const QFileInfo mediaInfo(m_sourcePath);
    const QString savedModified = project.value(QStringLiteral("sourceModifiedUtc")).toString();
    const bool sourceMatches = project.value(QStringLiteral("schemaVersion")).toInt() == 1
        && project.value(QStringLiteral("sourcePath")).toString() == m_sourcePath
        && project.value(QStringLiteral("sourceSizeBytes")).toVariant().toLongLong() == mediaInfo.size()
        && savedModified == mediaInfo.lastModified().toUTC().toString(Qt::ISODateWithMs);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || !sourceMatches) {
        setStatusMessage(QStringLiteral("The saved project does not match this source video; a fresh trim is ready."));
        return;
    }
    m_durationMs = project.value(QStringLiteral("durationMs")).toVariant().toLongLong();
    m_inMs = project.value(QStringLiteral("inMs")).toVariant().toLongLong();
    m_outMs = project.value(QStringLiteral("outMs")).toVariant().toLongLong();
    if (m_durationMs <= 0 || m_inMs < 0 || m_outMs <= m_inMs || m_outMs > m_durationMs) {
        m_durationMs = 0;
        m_inMs = 0;
        m_outMs = 0;
        setStatusMessage(QStringLiteral("The saved trim range is invalid; a fresh edit is ready."));
        return;
    }
    setDirty(false);
}

void VideoEditor::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void VideoEditor::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void VideoEditor::setExporting(bool exporting)
{
    if (m_exporting == exporting)
        return;
    m_exporting = exporting;
    emit exportingChanged();
}

void VideoEditor::setExportProgress(int progress)
{
    const int normalized = qBound(0, progress, 100);
    if (m_exportProgress == normalized)
        return;
    m_exportProgress = normalized;
    emit exportProgressChanged();
}

void VideoEditor::handleProcessOutput()
{
    m_stdoutBuffer.append(m_process.readAllStandardOutput());
    int newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.startsWith("out_time_us=")) {
            bool ok = false;
            const qint64 microseconds = line.mid(sizeof("out_time_us=") - 1).toLongLong(&ok);
            if (ok && m_outMs > m_inMs)
                setExportProgress(static_cast<int>((microseconds / 1000) * 100 / (m_outMs - m_inMs)));
        } else if (line.startsWith("out_time_ms=")) {
            bool ok = false;
            const qint64 microseconds = line.mid(sizeof("out_time_ms=") - 1).toLongLong(&ok);
            if (ok && m_outMs > m_inMs)
                setExportProgress(static_cast<int>((microseconds / 1000) * 100 / (m_outMs - m_inMs)));
        }
    }
}

void VideoEditor::finishExport(int exitCode, QProcess::ExitStatus exitStatus)
{
    handleProcessOutput();
    setExporting(false);
    if (m_cancelRequested) {
        QFile::remove(m_partialPath);
        setExportProgress(0);
        setStatusMessage(QStringLiteral("Export canceled. The source video was not changed."));
        return;
    }
    const QFileInfo partialInfo(m_partialPath);
    if (exitStatus != QProcess::NormalExit || exitCode != 0 || !partialInfo.exists() || partialInfo.size() <= 0) {
        QFile::remove(m_partialPath);
        setExportProgress(0);
        const QString details = QString::fromLocal8Bit(m_process.readAllStandardError()).trimmed();
        const QString message = details.isEmpty()
            ? QStringLiteral("FFmpeg could not export this trim range.") : details;
        setStatusMessage(message);
        emit exportFailed(message);
        return;
    }
    if (QFileInfo::exists(m_finalPath))
        QFile::remove(m_finalPath);
    if (!QFile::rename(m_partialPath, m_finalPath)) {
        QFile::remove(m_partialPath);
        const QString message = QStringLiteral("The export finished, but Moment could not finalize the output file.");
        setStatusMessage(message);
        emit exportFailed(message);
        return;
    }
    setExportProgress(100);
    QString replaceError;
    if (!m_library.replaceWithFile(m_sourcePath, m_finalPath, &replaceError)) {
        const QString message = QStringLiteral("Export finished, but Moment could not replace the Library item: %1")
                                    .arg(replaceError);
        setStatusMessage(message);
        emit exportFailed(message);
        return;
    }

    m_sourcePath = m_finalPath;
    m_projectPath = projectPathForSource(m_sourcePath);
    m_durationMs = m_outMs - m_inMs;
    m_inMs = 0;
    m_outMs = m_durationMs;
    emit sourceChanged();
    emit projectPathChanged();
    emit durationChanged();
    emit rangeChanged();
    setDirty(true);
    saveProject();
    setStatusMessage(QStringLiteral("Saved edit and replaced the Library item. Original file remains on disk."));
    emit exportSucceeded(m_finalPath);
}
