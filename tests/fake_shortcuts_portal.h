#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QSet>

struct PortalShortcut {
    QString id;
    QVariantMap properties;
};
Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(QList<PortalShortcut>)

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &item)
{
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &item)
{
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.endStructure();
    return argument;
}

class FakeShortcutsPortal : public QDBusVirtualObject {
public:
    QList<PortalShortcut> saved;
    QStringList boundIds;
    QString sessionPath;
    int listCalls = 0;
    int bindCalls = 0;
    int createCalls = 0;
    int closeCalls = 0;
    uint bindResponse = 0;
    QSet<QString> boundSessions;
    bool acceptBindings = true;

    QString introspect(const QString &) const override
    {
        return QStringLiteral(R"XML(
<interface name="org.freedesktop.portal.GlobalShortcuts">
 <method name="CreateSession"><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
 <method name="ListShortcuts"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
 <method name="BindShortcuts"><arg type="o" direction="in"/><arg type="a(sa{sv})" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
 <signal name="Activated"><arg type="o"/><arg type="s"/><arg type="t"/><arg type="a{sv}"/></signal>
</interface>)XML");
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override
    {
        const auto arguments = message.arguments();
        QVariantMap options;
        QVariantMap results;
        if (message.interface() == QStringLiteral("org.freedesktop.portal.Session")
            && message.member() == QStringLiteral("Close")) {
            ++closeCalls;
            bus.send(message.createReply());
            return true;
        }
        if (message.member() == QStringLiteral("CreateSession")) {
            ++createCalls;
            options = qdbus_cast<QVariantMap>(arguments.at(0));
            sessionPath = QStringLiteral("/org/freedesktop/portal/desktop/session/%1/%2")
                .arg(senderPath(message.service()), options.value("session_handle_token").toString());
            results.insert(QStringLiteral("session_handle"), sessionPath);
        } else if (message.member() == QStringLiteral("ListShortcuts")) {
            ++listCalls;
            options = qdbus_cast<QVariantMap>(arguments.at(1));
            results.insert(QStringLiteral("shortcuts"), QVariant::fromValue(saved));
        } else if (message.member() == QStringLiteral("BindShortcuts")) {
            const QString requestedSession = qdbus_cast<QDBusObjectPath>(arguments.at(0)).path();
            if (boundSessions.contains(requestedSession)) {
                bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.portal.Error.NotAllowed"),
                                                   QStringLiteral("Already bound this session")));
                return true;
            }
            boundSessions.insert(requestedSession);
            ++bindCalls;
            options = qdbus_cast<QVariantMap>(arguments.at(3));
            const auto definitions = qdbus_cast<QList<PortalShortcut>>(arguments.at(1));
            QList<PortalShortcut> accepted;
            for (const auto &item : definitions) {
                boundIds.append(item.id);
                QString trigger = item.properties.value(QStringLiteral("preferred_trigger")).toString();
                for (const auto &previous : saved)
                    if (previous.id == item.id)
                        trigger = previous.properties.value(QStringLiteral("trigger_description")).toString();
                if (!acceptBindings)
                    trigger.clear();
                accepted.append({item.id, {{QStringLiteral("trigger_description"), trigger}}});
            }
            results.insert(QStringLiteral("shortcuts"), QVariant::fromValue(accepted));
        } else {
            return false;
        }

        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
            .arg(senderPath(message.service()), options.value("handle_token").toString());
        bus.send(message.createReply({QVariant::fromValue(QDBusObjectPath(path))}));
        // Deliberately send immediately: consumers must subscribe before calling.
        auto response = QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.portal.Request"),
                                                   QStringLiteral("Response"));
        response << (message.member() == QStringLiteral("BindShortcuts") ? bindResponse : uint(0)) << results;
        bus.send(response);
        return true;
    }

    void activate(const QDBusConnection &bus, const QString &id)
    {
        if (!bindCalls)
            return;
        auto signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QStringLiteral("Activated"));
        signal << QVariant::fromValue(QDBusObjectPath(sessionPath)) << id
               << qulonglong(123) << QVariantMap{};
        bus.send(signal);
    }

    void change(const QDBusConnection &bus, const QList<PortalShortcut> &changed)
    {
        auto signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QStringLiteral("ShortcutsChanged"));
        signal << QVariant::fromValue(QDBusObjectPath(sessionPath)) << QVariant::fromValue(changed);
        bus.send(signal);
    }

private:
    static QString senderPath(QString sender)
    {
        sender.remove(0, 1);
        return sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    }
};

