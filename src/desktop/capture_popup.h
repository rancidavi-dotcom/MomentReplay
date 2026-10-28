#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <memory>

class QQmlEngine;
class QQuickView;

class CapturePopup final : public QObject {
    Q_OBJECT
public:
    explicit CapturePopup(QQmlEngine *engine, QObject *parent = nullptr);
    ~CapturePopup() override;

    void show(const QString &title, const QString &message, bool warning = false,
              const QUrl &preview = {});
    QQuickView *window() const;

private:
    std::unique_ptr<QQuickView> m_window;
    QTimer m_hideTimer;
    int m_sequence = 0;
};
