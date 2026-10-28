#pragma once

#include <QObject>
#include <QPointer>
#include <QMenu>
#include <QSystemTrayIcon>

class QWindow;

class TrayController final : public QObject {
    Q_OBJECT

public:
    explicit TrayController(QObject *parent = nullptr);

    void setMainWindow(QWindow *window);
    void showTrayIcon();
    Q_INVOKABLE bool hideWindowToTray();

signals:
    void quitRequested();

private:
    void showMainWindow();

    QPointer<QWindow> m_mainWindow;
    QSystemTrayIcon m_trayIcon;
    QMenu m_menu;
};
