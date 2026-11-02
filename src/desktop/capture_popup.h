#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QString>
#include <memory>

class QQmlEngine;
class QQuickView;

class CapturePopup final : public QObject {
    Q_OBJECT
public:
    explicit CapturePopup(QQmlEngine *engine, QObject *parent = nullptr,
                          const QString &scope = QStringLiteral("capture"),
                          int verticalOffset = 0);
    ~CapturePopup() override;

    void show(const QString &title, const QString &message, bool warning = false,
              const QUrl &preview = {}, bool persistent = false);
    QQuickView *window() const;

private:
    std::unique_ptr<QQuickView> m_window;
    QTimer m_hideTimer;
    int m_sequence = 0;
    int m_verticalOffset = 0;
};
