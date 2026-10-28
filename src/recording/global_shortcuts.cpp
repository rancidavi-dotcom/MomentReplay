#include "global_shortcuts.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QStringList>
#include <QTimer>
#include <QUuid>


namespace {
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kShortcutsInterface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString kRequestInterface = QStringLiteral("org.freedesktop.portal.Request");

struct ShortcutBinding {
    QString id;
    QVariantMap properties;
};

QDBusArgument &operator<<(QDBusArgument &argument, const ShortcutBinding &binding)
{
    argument.beginStructure();
    argument << binding.id << binding.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, ShortcutBinding &binding)
{
    argument.beginStructure();
    argument >> binding.id >> binding.properties;
    argument.endStructure();
    return argument;
}
}

Q_DECLARE_METATYPE(ShortcutBinding)
Q_DECLARE_METATYPE(QList<ShortcutBinding>)

GlobalShortcuts::GlobalShortcuts(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<ShortcutBinding>();
    qDBusRegisterMetaType<QList<ShortcutBinding>>();

    auto bus = QDBusConnection::sessionBus();
    const auto *busInterface = bus.interface();
    m_portalAvailable = busInterface
        && busInterface->isServiceRegistered(kPortalService).value();
    bus.connect(kPortalService, kPortalPath, kShortcutsInterface, QStringLiteral("Activated"),
                this, SLOT(handleActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    bus.connect(kPortalService, kPortalPath, kShortcutsInterface, QStringLiteral("ShortcutsChanged"),
                this, SLOT(handleShortcutsChanged(QDBusMessage)));
    bus.connect(kPortalService, QString(), QStringLiteral("org.freedesktop.portal.Session"),
                QStringLiteral("Closed"), this, SLOT(handleSessionClosed(QDBusMessage)));
    auto *serviceWatcher = new QDBusServiceWatcher(kPortalService, bus,
        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
        resetSession(false);
        m_portalAvailable = !newOwner.isEmpty();
        setStatus(m_portalAvailable ? QStringLiteral("Reconnecting global capture shortcuts…")
                                   : QStringLiteral("Global shortcut portal disconnected"));
        emit stateChanged();
        if (m_portalAvailable)
            requestCaptureShortcuts();
    });
    if (m_portalAvailable) {
        // Register both global actions even on first launch. The desktop owns
        // consent and restores previously approved key choices for this app ID.
        QTimer::singleShot(0, this, &GlobalShortcuts::requestCaptureShortcuts);
    } else {
        m_statusMessage = QStringLiteral("This session has no XDG Global Shortcuts portal");
    }
}

GlobalShortcuts::~GlobalShortcuts()
{
    resetSession(true);
}

bool GlobalShortcuts::portalAvailable() const { return m_portalAvailable; }
bool GlobalShortcuts::registered() const { return m_registered; }
bool GlobalShortcuts::screenshotRegistered() const { return m_screenshotRegistered; }
bool GlobalShortcuts::allRegistered() const { return m_registered && m_screenshotRegistered; }
bool GlobalShortcuts::requesting() const { return m_requesting; }
QString GlobalShortcuts::triggerDescription() const { return m_triggerDescription; }
QString GlobalShortcuts::screenshotTriggerDescription() const { return m_screenshotTriggerDescription; }
QString GlobalShortcuts::statusMessage() const { return m_statusMessage; }

void GlobalShortcuts::requestCaptureShortcuts()
{
    if (allRegistered() || m_requesting)
        return;
    if (!m_portalAvailable) {
        setStatus(QStringLiteral("Global shortcut portal is unavailable in this desktop session"));
        return;
    }

    // BindShortcuts is allowed once per session. Retrying a cancelled or
    // partially accepted setup must start a fresh session, with the same IDs.
    resetSession(true);
    m_requesting = true;
    setStatus(QStringLiteral("Registering global F8/F9 capture shortcuts…"));
    emit stateChanged();

    createSession();
}

void GlobalShortcuts::createSession()
{
    const QString token = QStringLiteral("moment_%1")
        .arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
    const QString requestToken = token + QStringLiteral("_req");
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), requestToken);
    options.insert(QStringLiteral("session_handle_token"), token + QStringLiteral("_session"));

    m_createRequestPath = requestPathForToken(requestToken);
    connectRequestResponse(m_createRequestPath,
                           SLOT(handleCreateSessionResponse(uint,QVariantMap)));
    auto method = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kShortcutsInterface,
                                                QStringLiteral("CreateSession"));
    method << options;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(method), this);
    const quint64 generation = m_generation;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QDBusObjectPath> reply = *finished;
        finished->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            disconnectRequestResponse(m_createRequestPath,
                                      SLOT(handleCreateSessionResponse(uint,QVariantMap)));
            m_createRequestPath.clear();
            m_requesting = false;
            setStatus(QStringLiteral("Portal could not create a shortcut session: %1").arg(reply.error().message()));
            emit stateChanged();
            return;
        }
        const QString returnedPath = reply.value().path();
        if (!m_createRequestPath.isEmpty() && returnedPath != m_createRequestPath) {
            disconnectRequestResponse(m_createRequestPath,
                                      SLOT(handleCreateSessionResponse(uint,QVariantMap)));
            m_createRequestPath = returnedPath;
            connectRequestResponse(m_createRequestPath,
                                   SLOT(handleCreateSessionResponse(uint,QVariantMap)));
        }
    });
}

void GlobalShortcuts::reportReplayUnavailable()
{
    setStatus(QStringLiteral("Replay is not active. Open Moment or retry screen capture before switching to your game."));
    emit stateChanged();
}

void GlobalShortcuts::handleCreateSessionResponse(uint response, const QVariantMap &results)
{
    disconnectRequestResponse(m_createRequestPath,
                              SLOT(handleCreateSessionResponse(uint,QVariantMap)));
    m_createRequestPath.clear();
    if (response != 0) {
        m_requesting = false;
        setStatus(response == 1 ? QStringLiteral("Shortcut request was cancelled")
                                : QStringLiteral("Portal rejected the shortcut request"));
        emit stateChanged();
        return;
    }

    const QVariant session = results.value(QStringLiteral("session_handle"));
    if (session.metaType() == QMetaType::fromType<QDBusObjectPath>())
        m_sessionPath = qvariant_cast<QDBusObjectPath>(session).path();
    else
        m_sessionPath = session.toString();
    if (m_sessionPath.isEmpty()) {
        m_requesting = false;
        setStatus(QStringLiteral("Portal created an empty shortcut session handle"));
        emit stateChanged();
        return;
    }
    bindReplayShortcut();
}

void GlobalShortcuts::bindReplayShortcut()
{
    QList<ShortcutBinding> bindings;
    {
        ShortcutBinding binding;
        binding.id = QStringLiteral("save-replay");
        binding.properties.insert(QStringLiteral("description"), QStringLiteral("Save the recent replay clip"));
        binding.properties.insert(QStringLiteral("preferred_trigger"), QStringLiteral("F8"));
        bindings.append(binding);
    }
    {
        ShortcutBinding screenshotBinding;
        screenshotBinding.id = QStringLiteral("save-screenshot");
        screenshotBinding.properties.insert(QStringLiteral("description"), QStringLiteral("Save a screenshot of the current screen"));
        screenshotBinding.properties.insert(QStringLiteral("preferred_trigger"), QStringLiteral("F9"));
        bindings.append(screenshotBinding);
    }
    QVariantMap options;
    const QString requestToken = QStringLiteral("moment_bind_%1")
        .arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
    options.insert(QStringLiteral("handle_token"), requestToken);

    m_bindRequestPath = requestPathForToken(requestToken);
    connectRequestResponse(m_bindRequestPath,
                           SLOT(handleBindShortcutsResponse(uint,QVariantMap)));
    auto method = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kShortcutsInterface,
                                                QStringLiteral("BindShortcuts"));
    method << QVariant::fromValue(QDBusObjectPath(m_sessionPath))
           << QVariant::fromValue(bindings) << QString() << options;
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(method), this);
    const quint64 generation = m_generation;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *finished) {
        QDBusPendingReply<QDBusObjectPath> reply = *finished;
        finished->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            disconnectRequestResponse(m_bindRequestPath,
                                      SLOT(handleBindShortcutsResponse(uint,QVariantMap)));
            m_bindRequestPath.clear();
            m_requesting = false;
            setStatus(QStringLiteral("Portal could not bind the replay shortcut: %1").arg(reply.error().message()));
            emit stateChanged();
            return;
        }
        const QString returnedPath = reply.value().path();
        if (!m_bindRequestPath.isEmpty() && returnedPath != m_bindRequestPath) {
            disconnectRequestResponse(m_bindRequestPath,
                                      SLOT(handleBindShortcutsResponse(uint,QVariantMap)));
            m_bindRequestPath = returnedPath;
            connectRequestResponse(m_bindRequestPath,
                                   SLOT(handleBindShortcutsResponse(uint,QVariantMap)));
        }
    });
}

void GlobalShortcuts::handleBindShortcutsResponse(uint response, const QVariantMap &results)
{
    disconnectRequestResponse(m_bindRequestPath,
                              SLOT(handleBindShortcutsResponse(uint,QVariantMap)));
    m_bindRequestPath.clear();
    m_requesting = false;
    if (response != 0) {
        setStatus(response == 1 ? QStringLiteral("Shortcut binding was cancelled")
                               : QStringLiteral("Portal did not bind the replay shortcut"));
        emit stateChanged();
        return;
    }

    applyShortcuts(results, QStringLiteral("Global capture shortcuts active"), true);
    emit stateChanged();
}

void GlobalShortcuts::applyShortcuts(const QVariantMap &results, const QString &statusPrefix,
                                     bool replaceCurrent)
{
    const QVariant shortcuts = results.value(QStringLiteral("shortcuts"));
    QList<ShortcutBinding> accepted;
    if (shortcuts.metaType() == QMetaType::fromType<QDBusArgument>())
        accepted = qdbus_cast<QList<ShortcutBinding>>(shortcuts);
    else if (shortcuts.canConvert<QList<ShortcutBinding>>())
        accepted = shortcuts.value<QList<ShortcutBinding>>();

    if (replaceCurrent) {
        m_registered = false;
        m_screenshotRegistered = false;
    }
    for (const ShortcutBinding &item : accepted) {
        const QString trigger = item.properties.value(QStringLiteral("trigger_description")).toString();
        if (item.id == QStringLiteral("save-replay")) {
            m_registered = !trigger.trimmed().isEmpty();
            if (!trigger.isEmpty())
                m_triggerDescription = trigger;
        } else if (item.id == QStringLiteral("save-screenshot")) {
            m_screenshotRegistered = !trigger.trimmed().isEmpty();
            if (!trigger.isEmpty())
                m_screenshotTriggerDescription = trigger;
        }
    }
    if (!m_registered && !m_screenshotRegistered) {
        setStatus(QStringLiteral("No replay shortcut was accepted by the system"));
        return;
    }
    QStringList active;
    if (m_registered)
        active.append(QStringLiteral("replay %1").arg(m_triggerDescription));
    if (m_screenshotRegistered)
        active.append(QStringLiteral("screenshot %1").arg(m_screenshotTriggerDescription));
    setStatus(QStringLiteral("%1 · %2").arg(statusPrefix, active.join(QStringLiteral(" · "))));
}

void GlobalShortcuts::resetSession(bool closeRemote)
{
    ++m_generation;
    disconnectRequestResponse(m_createRequestPath,
                              SLOT(handleCreateSessionResponse(uint,QVariantMap)));
    disconnectRequestResponse(m_bindRequestPath,
                              SLOT(handleBindShortcutsResponse(uint,QVariantMap)));
    m_createRequestPath.clear();
    m_bindRequestPath.clear();
    if (closeRemote && !m_sessionPath.isEmpty()) {
        const auto close = QDBusMessage::createMethodCall(kPortalService, m_sessionPath,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
        QDBusConnection::sessionBus().asyncCall(close);
    }
    m_sessionPath.clear();
    m_registered = false;
    m_screenshotRegistered = false;
    m_requesting = false;
}

void GlobalShortcuts::handleShortcutsChanged(const QDBusMessage &message)
{
    const auto arguments = message.arguments();
    if (arguments.size() != 2 || m_sessionPath.isEmpty()
        || qdbus_cast<QDBusObjectPath>(arguments.at(0)).path() != m_sessionPath)
        return;
    // The signal contains changed actions, not necessarily the complete list.
    applyShortcuts({{QStringLiteral("shortcuts"), arguments.at(1)}},
                   QStringLiteral("Global capture shortcuts active"), false);
    emit stateChanged();
}

void GlobalShortcuts::handleSessionClosed(const QDBusMessage &message)
{
    if (m_sessionPath.isEmpty() || message.path() != m_sessionPath)
        return;
    resetSession(false);
    setStatus(QStringLiteral("Global shortcut session ended. Enable capture shortcuts to reconnect."));
    emit stateChanged();
}

void GlobalShortcuts::handleActivated(const QDBusObjectPath &session, const QString &shortcutId,
                                      qulonglong timestamp, const QVariantMap &options)
{
    Q_UNUSED(timestamp);
    Q_UNUSED(options);
    qInfo().noquote() << "Moment: global shortcut signal received:" << shortcutId
                      << "session=" << session.path();
    if (session.path() != m_sessionPath) {
        qWarning().noquote() << "Moment: ignoring shortcut from an unexpected portal session; expected"
                             << m_sessionPath;
        return;
    }
    if (shortcutId == QStringLiteral("save-replay") && m_registered)
        emit saveReplayRequested();
    else if (shortcutId == QStringLiteral("save-screenshot") && m_screenshotRegistered)
        emit saveScreenshotRequested();
    else
        qWarning().noquote() << "Moment: shortcut ID is not currently registered:" << shortcutId;
}

void GlobalShortcuts::connectRequestResponse(const QString &requestPath, const char *slot)
{
    QDBusConnection::sessionBus().connect(kPortalService, requestPath, kRequestInterface,
                                          QStringLiteral("Response"), this, slot);
}

void GlobalShortcuts::disconnectRequestResponse(const QString &requestPath, const char *slot)
{
    if (!requestPath.isEmpty())
        QDBusConnection::sessionBus().disconnect(kPortalService, requestPath, kRequestInterface,
                                                  QStringLiteral("Response"), this, slot);
}

QString GlobalShortcuts::requestPathForToken(const QString &token) const
{
    QString sender = QDBusConnection::sessionBus().baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return kPortalPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;
}

void GlobalShortcuts::setStatus(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
}
