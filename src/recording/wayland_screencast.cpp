#include "wayland_screencast.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

namespace {

constexpr auto portalService = "org.freedesktop.portal.Desktop";
constexpr auto portalPath = "/org/freedesktop/portal/desktop";
constexpr auto screenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr auto requestInterface = "org.freedesktop.portal.Request";
constexpr auto sessionInterface = "org.freedesktop.portal.Session";
struct StreamInfo {
    quint32 nodeId = 0;
    QVariantMap properties;
};

StreamInfo firstStream(const QVariantMap &results)
{
    StreamInfo stream;
    const QVariant value = results.value(QStringLiteral("streams"));
    if (!value.canConvert<QDBusArgument>())
        return stream;

    const QDBusArgument array = value.value<QDBusArgument>();
    array.beginArray();
    if (!array.atEnd()) {
        array.beginStructure();
        array >> stream.nodeId >> stream.properties;
        array.endStructure();
    }
    array.endArray();
    return stream;
}

QSize streamSize(const QVariantMap &properties)
{
    const QVariant value = properties.value(QStringLiteral("size"));
    if (!value.canConvert<QDBusArgument>())
        return {};

    int width = 0;
    int height = 0;
    const QDBusArgument tuple = value.value<QDBusArgument>();
    tuple.beginStructure();
    tuple >> width >> height;
    tuple.endStructure();
    return QSize(width, height);
}

GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData)
{
    auto *capture = static_cast<WaylandScreenCast *>(userData);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
        return GST_FLOW_EOS;

    GstVideoInfo info;
    GstVideoFrame frame;
    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!caps || !buffer || !gst_video_info_from_caps(&info, caps)
        || !gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    const auto *data = static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
    const QImage borrowed(data, static_cast<int>(GST_VIDEO_INFO_WIDTH(&info)),
                         static_cast<int>(GST_VIDEO_INFO_HEIGHT(&info)), stride,
                         QImage::Format_ARGB32);
    const QImage image = borrowed.copy();
    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    const qint64 presentationTimeUs = GST_CLOCK_TIME_IS_VALID(pts)
        ? static_cast<qint64>(gst_util_uint64_scale(pts, 1, GST_USECOND)) : -1;

    gst_video_frame_unmap(&frame);
    gst_sample_unref(sample);

    if (!image.isNull())
        emit capture->frameReady(image, presentationTimeUs);
    return GST_FLOW_OK;
}

}

WaylandScreenCast::WaylandScreenCast(QString restoreTokenPath, QObject *parent)
    : QObject(parent), m_bus(QDBusConnection::sessionBus()),
      m_restoreTokenPath(std::move(restoreTokenPath)), m_busTimer(new QTimer(this))
{
    m_busTimer->setInterval(500);
    connect(m_busTimer, &QTimer::timeout, this, &WaylandScreenCast::checkPipeWireBus);
    connect(this, &WaylandScreenCast::frameReady, this,
            [this](const QImage &, qint64) {
        if (m_receivedFrame)
            return;
        m_receivedFrame = true;
        qInfo() << "Moment received the first screen frame from PipeWire.";
    }, Qt::QueuedConnection);
}

WaylandScreenCast::~WaylandScreenCast()
{
    stopPipeWirePipeline();
    closePortalSession();
}

bool WaylandScreenCast::active() const
{
    return m_active;
}

void WaylandScreenCast::start()
{
    if (m_active || m_requestStage != RequestStage::None || !m_sessionPath.isEmpty())
        return;

    if (!m_bus.isConnected()) {
        fail(QStringLiteral("The desktop session bus is unavailable."));
        return;
    }
    m_cancelled = false;

    static bool gstInitialized = false;
    if (!gstInitialized) {
        GError *error = nullptr;
        if (!gst_init_check(nullptr, nullptr, &error)) {
            const QString message = error ? QString::fromUtf8(error->message)
                                          : QStringLiteral("GStreamer could not initialize.");
            if (error)
                g_error_free(error);
            fail(message);
            return;
        }
        gstInitialized = true;
    }

    QDBusInterface properties(portalService, portalPath,
                              QStringLiteral("org.freedesktop.DBus.Properties"), m_bus);
    const QDBusMessage versionReply = properties.call(
        QStringLiteral("Get"), QString::fromLatin1(screenCastInterface), QStringLiteral("version"));
    quint32 portalVersion = 0;
    if (versionReply.type() == QDBusMessage::ReplyMessage
        && !versionReply.arguments().isEmpty()) {
        const QVariant wrapped = versionReply.arguments().first();
        if (wrapped.canConvert<QDBusVariant>())
            portalVersion = wrapped.value<QDBusVariant>().variant().toUInt();
        else
            portalVersion = wrapped.toUInt();
    }
    m_portalVersion = portalVersion;
    if (portalVersion < 1) {
        fail(QStringLiteral("The desktop does not provide the ScreenCast portal."));
        return;
    }

    QVariantMap options;
    const QString token = QUuid::createUuid().toString(QUuid::Id128);
    options.insert(QStringLiteral("handle_token"), QStringLiteral("moment_create_%1").arg(token));
    options.insert(QStringLiteral("session_handle_token"), QStringLiteral("moment_session_%1").arg(token));
    callPortal(QStringLiteral("CreateSession"), {options}, RequestStage::CreateSession);
}

void WaylandScreenCast::stop()
{
    m_cancelled = true;
    m_busTimer->stop();
    if (!m_requestPath.isEmpty()) {
        m_bus.disconnect(QString::fromLatin1(portalService), m_requestPath,
                         QString::fromLatin1(requestInterface), QStringLiteral("Response"),
                         this, SLOT(handlePortalResponse(uint,QVariantMap)));
        QDBusMessage closeRequest = QDBusMessage::createMethodCall(
            QString::fromLatin1(portalService), m_requestPath,
            QString::fromLatin1(requestInterface), QStringLiteral("Close"));
        m_bus.asyncCall(closeRequest);
    }
    m_requestPath.clear();
    m_requestStage = RequestStage::None;
    stopPipeWirePipeline();
    closePortalSession();
    m_pipeWireFd = QDBusUnixFileDescriptor();
    m_requestStage = RequestStage::None;
    m_requestPath.clear();
    if (m_active) {
        m_active = false;
        emit activeChanged(false);
    }
}

void WaylandScreenCast::callPortal(const QString &method, const QVariantList &arguments,
                                   RequestStage stage)
{
    if (arguments.isEmpty() || !arguments.constLast().canConvert<QVariantMap>()) {
        fail(QStringLiteral("Invalid ScreenCast portal request options."));
        return;
    }
    const QString handleToken = arguments.constLast().toMap()
        .value(QStringLiteral("handle_token")).toString();
    if (handleToken.isEmpty()) {
        fail(QStringLiteral("ScreenCast portal request has no handle token."));
        return;
    }

    QString sender = m_bus.baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString expectedRequestPath = QStringLiteral("%1/request/%2/%3")
        .arg(QString::fromLatin1(portalPath), sender, handleToken);

    // Subscribe before calling the portal. XDG Desktop Portal uses the sender
    // and handle_token to make this path predictable, specifically to prevent
    // losing a fast Response signal between the method reply and subscription.
    if (!m_requestPath.isEmpty())
        m_bus.disconnect(QString::fromLatin1(portalService), m_requestPath,
                         QString::fromLatin1(requestInterface), QStringLiteral("Response"),
                         this, SLOT(handlePortalResponse(uint,QVariantMap)));
    m_requestPath = expectedRequestPath;
    m_requestStage = stage;
    const bool connected = m_bus.connect(
        QString::fromLatin1(portalService), m_requestPath,
        QString::fromLatin1(requestInterface), QStringLiteral("Response"), this,
        SLOT(handlePortalResponse(uint,QVariantMap)));
    if (!connected) {
        m_requestPath.clear();
        m_requestStage = RequestStage::None;
        fail(QStringLiteral("Could not subscribe to the ScreenCast portal response."));
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService), QString::fromLatin1(portalPath),
        QString::fromLatin1(screenCastInterface), method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 25000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, stage, expectedRequestPath] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            if (m_requestStage != stage)
                return;
            if (!m_requestPath.isEmpty())
                m_bus.disconnect(QString::fromLatin1(portalService), m_requestPath,
                                 QString::fromLatin1(requestInterface), QStringLiteral("Response"),
                                 this, SLOT(handlePortalResponse(uint,QVariantMap)));
            m_requestPath.clear();
            if (stage == RequestStage::SelectSources || stage == RequestStage::Start)
                clearRestoreToken();
            m_requestStage = RequestStage::None;
            fail(QStringLiteral("ScreenCast portal request failed: %1")
                     .arg(reply.error().message()));
            return;
        }

        if (m_requestStage != stage || m_requestPath.isEmpty())
            return; // The response arrived before this completion callback.

        if (m_cancelled) {
            QDBusMessage closeRequest = QDBusMessage::createMethodCall(
                QString::fromLatin1(portalService), reply.value().path(),
                QString::fromLatin1(requestInterface), QStringLiteral("Close"));
            m_bus.asyncCall(closeRequest);
            return;
        }

        const QString returnedPath = reply.value().path();
        if (returnedPath != expectedRequestPath) {
            m_bus.disconnect(QString::fromLatin1(portalService), m_requestPath,
                             QString::fromLatin1(requestInterface), QStringLiteral("Response"),
                             this, SLOT(handlePortalResponse(uint,QVariantMap)));
            m_requestPath = returnedPath;
            qWarning().noquote() << "ScreenCast portal returned an unexpected request path;"
                                    " subscribing after the method reply. Expected:"
                                 << expectedRequestPath << "received:" << returnedPath;
            const bool fallbackConnected = m_bus.connect(
                QString::fromLatin1(portalService), m_requestPath,
                QString::fromLatin1(requestInterface), QStringLiteral("Response"), this,
                SLOT(handlePortalResponse(uint,QVariantMap)));
            if (!fallbackConnected) {
                m_requestPath.clear();
                m_requestStage = RequestStage::None;
                fail(QStringLiteral("Could not subscribe to the ScreenCast portal response."));
            }
        }
    });
}

void WaylandScreenCast::handlePortalResponse(uint response, const QVariantMap &results)
{
    const RequestStage stage = m_requestStage;
    if (!m_requestPath.isEmpty())
        m_bus.disconnect(QString::fromLatin1(portalService), m_requestPath,
                         QString::fromLatin1(requestInterface), QStringLiteral("Response"),
                         this, SLOT(handlePortalResponse(uint,QVariantMap)));
    m_requestPath.clear();
    m_requestStage = RequestStage::None;

    if (response != 0) {
        if (stage == RequestStage::Start || stage == RequestStage::SelectSources)
            clearRestoreToken();
        fail(response == 1 ? QStringLiteral("Screen sharing was cancelled.")
                           : QStringLiteral("The ScreenCast portal rejected the request."));
        return;
    }

    if (stage == RequestStage::CreateSession) {
        const QVariant sessionValue = results.value(QStringLiteral("session_handle"));
        if (sessionValue.canConvert<QDBusObjectPath>())
            m_sessionPath = sessionValue.value<QDBusObjectPath>().path();
        else
            m_sessionPath = sessionValue.toString();
        if (m_sessionPath.isEmpty()) {
            fail(QStringLiteral("The ScreenCast portal returned no session."));
            return;
        }

        QVariantMap options;
        options.insert(QStringLiteral("handle_token"), QStringLiteral("moment_sources_%1")
                           .arg(QUuid::createUuid().toString(QUuid::Id128)));
        options.insert(QStringLiteral("types"), QVariant::fromValue<quint32>(1));
        options.insert(QStringLiteral("multiple"), false);
        if (m_portalVersion >= 4)
            options.insert(QStringLiteral("persist_mode"), QVariant::fromValue<quint32>(2));
        m_restoreToken = readRestoreToken();
        if (m_portalVersion >= 4 && !m_restoreToken.isEmpty())
            options.insert(QStringLiteral("restore_token"), m_restoreToken);
        callPortal(QStringLiteral("SelectSources"),
                   {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), options},
                   RequestStage::SelectSources);
        return;
    }

    if (stage == RequestStage::SelectSources) {
        QVariantMap options;
        options.insert(QStringLiteral("handle_token"), QStringLiteral("moment_start_%1")
                           .arg(QUuid::createUuid().toString(QUuid::Id128)));
        callPortal(QStringLiteral("Start"),
                   {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QString(), options},
                   RequestStage::Start);
        return;
    }

    if (stage == RequestStage::Start) {
        const QString newToken = results.value(QStringLiteral("restore_token")).toString();
        if (newToken.isEmpty())
            clearRestoreToken();
        else
            saveRestoreToken(newToken);
        openPipeWire(results);
    }
}

void WaylandScreenCast::openPipeWire(const QVariantMap &results)
{
    const StreamInfo stream = firstStream(results);
    if (stream.nodeId == 0 && stream.properties.isEmpty()) {
        fail(QStringLiteral("The ScreenCast portal returned no video stream."));
        return;
    }
    emit streamReady(streamSize(stream.properties));

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService), QString::fromLatin1(portalPath),
        QString::fromLatin1(screenCastInterface), QStringLiteral("OpenPipeWireRemote"));
    message.setArguments({QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QVariantMap{}});
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, stream] {
        QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError() || !reply.value().isValid()) {
            fail(QStringLiteral("Could not open the portal's PipeWire stream: %1")
                     .arg(reply.isError() ? reply.error().message() : QStringLiteral("invalid file descriptor")));
            return;
        }
        m_pipeWireFd = reply.value();
        if (!startPipeWirePipeline(stream.nodeId, stream.properties))
            return;
        m_receivedFrame = false;
        m_captureStartedAt.start();
        m_active = true;
        m_busTimer->start();
        emit activeChanged(true);
    });
}

bool WaylandScreenCast::startPipeWirePipeline(quint32 nodeId, const QVariantMap &properties)
{
    GstElement *source = gst_element_factory_make("pipewiresrc", "moment-screen-source");
    GstElement *convert = gst_element_factory_make("videoconvert", "moment-screen-convert");
    GstElement *filter = gst_element_factory_make("capsfilter", "moment-screen-caps");
    GstElement *sink = gst_element_factory_make("appsink", "moment-screen-sink");
    if (!source || !convert || !filter || !sink) {
        if (source) gst_object_unref(source);
        if (convert) gst_object_unref(convert);
        if (filter) gst_object_unref(filter);
        if (sink) gst_object_unref(sink);
        fail(QStringLiteral("GStreamer is missing the PipeWire screen-capture elements."));
        return false;
    }

    m_pipeline = gst_pipeline_new("moment-screen-capture");
    if (!m_pipeline) {
        gst_object_unref(source);
        gst_object_unref(convert);
        gst_object_unref(filter);
        gst_object_unref(sink);
        fail(QStringLiteral("Could not create a PipeWire capture pipeline."));
        return false;
    }

    g_object_set(source, "fd", m_pipeWireFd.fileDescriptor(), nullptr);
    const QVariant serial = properties.value(QStringLiteral("pipewire-serial"));
    const QString target = serial.isValid() ? QString::number(serial.toULongLong())
                                            : QString::number(nodeId);
    g_object_set(source, "target-object", target.toUtf8().constData(), nullptr);

    GstCaps *caps = gst_caps_from_string("video/x-raw,format=BGRA");
    g_object_set(filter, "caps", caps, nullptr);
    gst_caps_unref(caps);
    g_object_set(sink, "emit-signals", FALSE, "sync", FALSE,
                 "max-buffers", 2u, "drop", TRUE, nullptr);
    GstAppSinkCallbacks callbacks{};
    callbacks.new_sample = onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);

    gst_bin_add_many(GST_BIN(m_pipeline), source, convert, filter, sink, nullptr);
    if (!gst_element_link_many(source, convert, filter, sink, nullptr)) {
        stopPipeWirePipeline();
        fail(QStringLiteral("Could not connect the PipeWire video pipeline."));
        return false;
    }

    const GstStateChangeReturn state = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (state == GST_STATE_CHANGE_FAILURE) {
        stopPipeWirePipeline();
        fail(QStringLiteral("Could not start receiving frames from PipeWire."));
        return false;
    }
    return true;
}

void WaylandScreenCast::stopPipeWirePipeline()
{
    m_busTimer->stop();
    if (!m_pipeline)
        return;
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gst_object_unref(m_pipeline);
    m_pipeline = nullptr;
}

void WaylandScreenCast::closePortalSession()
{
    if (m_sessionPath.isEmpty())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService), m_sessionPath,
        QString::fromLatin1(sessionInterface), QStringLiteral("Close"));
    m_bus.asyncCall(message);
    m_sessionPath.clear();
}

QString WaylandScreenCast::readRestoreToken() const
{
    QFile file(m_restoreTokenPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

void WaylandScreenCast::saveRestoreToken(const QString &token)
{
    const QFileInfo info(m_restoreTokenPath);
    if (!QDir().mkpath(info.absolutePath())) {
        clearRestoreToken();
        return;
    }
    QSaveFile file(m_restoreTokenPath);
    if (!file.open(QIODevice::WriteOnly)) {
        clearRestoreToken();
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(token.toUtf8()) != token.size() || !file.commit()) {
        clearRestoreToken();
        return;
    }
    QFile::setPermissions(m_restoreTokenPath,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void WaylandScreenCast::clearRestoreToken()
{
    QFile::remove(m_restoreTokenPath);
    m_restoreToken.clear();
}

void WaylandScreenCast::fail(const QString &message)
{
    qWarning().noquote() << "Moment screen capture failed:" << message;
    stopPipeWirePipeline();
    closePortalSession();
    m_pipeWireFd = QDBusUnixFileDescriptor();
    if (m_active) {
        m_active = false;
        emit activeChanged(false);
    }
    emit failed(message);
}

void WaylandScreenCast::checkPipeWireBus()
{
    if (!m_pipeline)
        return;
    if (!m_receivedFrame && m_captureStartedAt.isValid()
        && m_captureStartedAt.elapsed() >= 8000) {
        fail(QStringLiteral("The desktop granted a PipeWire screen stream, but no video frames arrived within 8 seconds."));
        return;
    }
    GstBus *bus = gst_element_get_bus(m_pipeline);
    GstMessage *message = gst_bus_pop_filtered(
        bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    gst_object_unref(bus);
    if (!message)
        return;

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        const QString detail = error ? QString::fromUtf8(error->message)
                                     : QStringLiteral("PipeWire stopped delivering frames.");
        if (error)
            g_error_free(error);
        g_free(debug);
        gst_message_unref(message);
        fail(detail);
        return;
    }
    gst_message_unref(message);
    fail(QStringLiteral("The PipeWire screen stream ended."));
}
