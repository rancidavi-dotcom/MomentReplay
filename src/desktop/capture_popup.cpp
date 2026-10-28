#include "capture_popup.h"

#include <QCursor>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QDebug>

#ifdef MOMENT_HAS_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

CapturePopup::CapturePopup(QQmlEngine *engine, QObject *parent)
    : QObject(parent), m_window(std::make_unique<QQuickView>(engine, nullptr))
{
    m_window->setTitle(QStringLiteral("Moment capture popup"));
    m_window->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
    m_window->setColor(Qt::transparent);
    m_window->setResizeMode(QQuickView::SizeViewToRootObject);
#ifdef MOMENT_HAS_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        auto *layer = LayerShellQt::Window::get(m_window.get());
        layer->setScope(QStringLiteral("moment-capture-popup"));
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorRight)
                          | LayerShellQt::Window::AnchorBottom);
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setMargins(QMargins(24, 24, 24, 24));
        layer->setExclusiveZone(0);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
        layer->setWantsToBeOnActiveScreen(true);
    }
#endif
    m_window->setSource(QUrl(QStringLiteral("qrc:/qt/qml/MedalLinux/qml/CapturePopup.qml")));
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(4200);
    connect(&m_hideTimer, &QTimer::timeout, m_window.get(), &QWindow::hide);
}

CapturePopup::~CapturePopup() = default;

void CapturePopup::show(const QString &title, const QString &message, bool warning,
                        const QUrl &preview)
{
    auto *item = m_window->rootObject();
    if (!item) {
        qWarning() << "Moment could not load its capture popup:" << m_window->errors();
        return;
    }
    item->setProperty("heading", title);
    item->setProperty("message", message);
    item->setProperty("isWarning", warning);
    item->setProperty("previewSource", preview);
    if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            m_window->setScreen(screen);
            const QRect area = screen->availableGeometry();
            m_window->setPosition(area.right() - m_window->width() - 23,
                                  area.bottom() - m_window->height() - 23);
        }
    }
    m_window->show();
    item->setProperty("sequence", ++m_sequence);
    m_hideTimer.start();
}

QQuickView *CapturePopup::window() const { return m_window.get(); }
