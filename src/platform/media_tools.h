#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

inline QString momentMediaTool(const QString &name)
{
    // The Windows portable package carries its command-line media tools alongside Moment.exe.
#ifdef Q_OS_WIN
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(name + ".exe");
    if (QFileInfo(bundled).isFile())
        return bundled;
#endif
    return QStandardPaths::findExecutable(name);
}
