#pragma once

#include <QObject>
#include <QByteArray>
#include <QProcess>
#include <QTimer>
#include <QString>
#include <QUrl>

class ClipLibraryModel;

class VideoEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int exportPreset READ exportPreset WRITE setExportPreset NOTIFY exportPresetChanged)
    Q_PROPERTY(QString exportPresetName READ exportPresetName NOTIFY exportPresetChanged)
    Q_PROPERTY(QString exportPresetDescription READ exportPresetDescription NOTIFY exportPresetChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourceChanged)
    Q_PROPERTY(QUrl sourceUrl READ sourceUrl NOTIFY sourceChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs WRITE setDurationMs NOTIFY durationChanged)
    Q_PROPERTY(qint64 inMs READ inMs WRITE setInMs NOTIFY rangeChanged)
    Q_PROPERTY(qint64 outMs READ outMs WRITE setOutMs NOTIFY rangeChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(int exportProgress READ exportProgress NOTIFY exportProgressChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectPathChanged)

public:
    VideoEditor(ClipLibraryModel &library, QString projectsDirectory,
                QString exportDirectory, QObject *parent = nullptr);
    ~VideoEditor() override;

    QString sourcePath() const;
    QUrl sourceUrl() const;
    qint64 durationMs() const;
    qint64 inMs() const;
    qint64 outMs() const;
    bool dirty() const;
    bool exporting() const;
    int exportProgress() const;
    QString statusMessage() const;
    QString projectPath() const;
    int exportPreset() const;
    QString exportPresetName() const;
    QString exportPresetDescription() const;

    Q_INVOKABLE void setDurationMs(qint64 duration);
    void setInMs(qint64 position);
    void setOutMs(qint64 position);

    Q_INVOKABLE void openClip(const QString &path);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool exportClip();
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void setExportPreset(int preset);

signals:
    void sourceChanged();
    void durationChanged();
    void rangeChanged();
    void dirtyChanged();
    void exportingChanged();
    void exportProgressChanged();
    void statusMessageChanged();
    void projectPathChanged();
    void exportPresetChanged();
    void exportSucceeded(const QString &path);
    void exportFailed(const QString &reason);

private:
    QString projectPathForSource(const QString &path) const;
    void loadProject();
    void setDirty(bool dirty);
    void setStatusMessage(const QString &message);
    void setExporting(bool exporting);
    void setExportProgress(int progress);
    void handleProcessOutput();
    void finishExport(int exitCode, QProcess::ExitStatus exitStatus);

    ClipLibraryModel &m_library;
    QString m_projectsDirectory;
    QString m_exportDirectory;
    QString m_sourcePath;
    QString m_projectPath;
    QString m_partialPath;
    QString m_finalPath;
    qint64 m_durationMs = 0;
    qint64 m_inMs = 0;
    qint64 m_outMs = 0;
    bool m_dirty = false;
    bool m_exporting = false;
    bool m_cancelRequested = false;
    int m_exportProgress = 0;
    int m_exportPreset = 1;
    QByteArray m_stdoutBuffer;
    QString m_statusMessage = QStringLiteral("Choose a video to begin editing.");
    QProcess m_process;
    QTimer m_projectSaveTimer;
};
