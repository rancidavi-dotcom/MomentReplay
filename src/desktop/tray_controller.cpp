#include "tray_controller.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QWindow>

TrayController::TrayController(QObject *parent)
    : QObject(parent)
{
    m_trayIcon.setIcon(QIcon(QStringLiteral(":/icons/io.moment.Moment.svg")));
    m_trayIcon.setToolTip(QStringLiteral("Moment — replay capture is running"));
    m_trayIcon.setContextMenu(&m_menu);

    QAction *openAction = m_menu.addAction(QStringLiteral("Open Moment"));
    connect(openAction, &QAction::triggered, this, &TrayController::showMainWindow);
    m_menu.addSeparator();
    QAction *quitAction = m_menu.addAction(QStringLiteral("Quit Moment"));
    connect(quitAction, &QAction::triggered, this, &TrayController::quitRequested);

    connect(&m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showMainWindow();
    });
}

void TrayController::setMainWindow(QWindow *window)
{
    m_mainWindow = window;
}

void TrayController::showTrayIcon()
{
    m_trayIcon.show();
}

bool TrayController::hideWindowToTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        emit quitRequested();
        return false;
    }
    if (!m_trayIcon.isVisible())
        m_trayIcon.show();
    if (m_mainWindow)
        m_mainWindow->hide();
    return true;
}

void TrayController::showMainWindow()
{
    if (!m_mainWindow)
        return;
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->requestActivate();
}
