#include "instant_replay.h"
#include "encoding_profile.h"
#include "../platform/media_tools.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QMediaFormat>
#include <QProcess>
#include <QVideoFrameFormat>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstring>
#include <limits>

#ifdef Q_OS_LINUX
namespace {

bool writePcmWave(const QString &path, const QVector<QAudioBuffer> &buffers,
                  const QAudioFormat &format, qint64 durationUs, QString *error)
{
    const int channels = format.channelCount();
    const int sampleRate = format.sampleRate();
    const int bytesPerSample = format.bytesPerSample();
    if (channels <= 0 || sampleRate <= 0 || bytesPerSample <= 0) {
        if (error) *error = QStringLiteral("The selected audio format cannot be exported.");
        return false;
    }

    quint16 formatTag = 1;
    const quint16 bitsPerSample = static_cast<quint16>(bytesPerSample * 8);
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
    case QAudioFormat::Int16:
    case QAudioFormat::Int32:
        break;
    case QAudioFormat::Float:
        formatTag = 3;
        break;
    default:
        if (error) *error = QStringLiteral("The selected audio sample format is unsupported.");
        return false;
    }

    const qint64 outputFrames = qMax<qint64>(1, durationUs * sampleRate / 1000000);
    const qint64 byteCount64 = outputFrames * channels * bytesPerSample;
    if (byteCount64 > std::numeric_limits<quint32>::max() - 36) {
        if (error) *error = QStringLiteral("The audio replay is too large to package as WAV.");
        return false;
    }
    const qsizetype bytesPerFrame = channels * bytesPerSample;
    QByteArray pcm(static_cast<qsizetype>(byteCount64),
                   format.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : char(0));
    for (const QAudioBuffer &buffer : buffers) {
        if (!buffer.isValid() || buffer.format() != format || buffer.frameCount() <= 0)
            continue;
        const qint64 targetFrame = buffer.startTime() * sampleRate / 1000000;
        const qint64 sourceFrame = qMax<qint64>(0, -targetFrame);
        const qint64 destinationFrame = qMax<qint64>(0, targetFrame);
        const qint64 copyFrames = qMin<qint64>(buffer.frameCount() - sourceFrame,
                                                outputFrames - destinationFrame);
        if (copyFrames <= 0)
            continue;
        const char *source = buffer.constData<char>() + sourceFrame * bytesPerFrame;
        char *destination = pcm.data() + destinationFrame * bytesPerFrame;
        std::memcpy(destination, source, static_cast<size_t>(copyFrames * bytesPerFrame));
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Could not create the temporary replay audio file.");
        return false;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcm.size());
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << formatTag << quint16(channels) << quint32(sampleRate)
           << quint32(sampleRate * bytesPerFrame) << quint16(bytesPerFrame) << bitsPerSample;
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    if (stream.writeRawData(pcm.constData(), pcm.size()) != pcm.size()
        || stream.status() != QDataStream::Ok) {
        if (error) *error = QStringLiteral("Could not write the temporary replay audio file.");
        return false;
    }
    return true;
}

} // namespace
#endif

InstantReplay::InstantReplay(QObject *parent)
    : QObject(parent), m_exportRecorder(this)
{
    m_clock.start();
    m_exportPump.setTimerType(Qt::PreciseTimer);
    connect(&m_exportPump, &QTimer::timeout, this, &InstantReplay::sendExportFrames);
    m_audioExportPump.setTimerType(Qt::PreciseTimer);
    m_audioExportPump.setInterval(10);
    connect(&m_audioExportPump, &QTimer::timeout, this, &InstantReplay::sendExportAudio);
    m_exportSession.setRecorder(&m_exportRecorder);

    connect(&m_frameEncoder, &QFutureWatcher<Frame>::finished, this, [this] {
        Frame frame = m_frameEncoder.result();
        m_encodingFrame = false;
        if (!frame.jpeg.isEmpty())
            appendEncodedFrame(std::move(frame));
        startEncodingPendingFrame();
    });

    connect(&m_exportPreparation, &QFutureWatcher<PreparedExport>::finished, this, [this] {
        startPreparedExport(m_exportPreparation.result());
    });
    connect(&m_exportFrameDecoder, &QFutureWatcher<QImage>::finished, this, [this] {
        m_pendingExportImage = m_exportFrameDecoder.result();
        m_decodingExportFrame = false;
        if (m_pendingExportImage.isNull()) {
            failExport(QStringLiteral("A replay frame could not be decoded."));
        }
    });

    connect(&m_exportRecorder, &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::RecordingState)
            startExportPump();
        else if (state == QMediaRecorder::StoppedState && m_finalizeRequested)
            finishExport();
    });
    connect(&m_exportRecorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &message) {
        failExport(message.isEmpty() ? QStringLiteral("Clip encoding failed.") : message);
    });
    connect(&m_exportProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &InstantReplay::finishExternalExport);
    connect(&m_exportProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_exporting)
            failExport(QStringLiteral("FFmpeg could not start to export the replay."));
    });
}

int InstantReplay::bufferSeconds() const
{
    return m_bufferSeconds;
}

int InstantReplay::bufferedSeconds() const
{
    if (m_frames.size() < 2)
        return 0;
    return static_cast<int>((m_frames.constLast().timestampMs - m_frames.constFirst().timestampMs) / 1000);
}

bool InstantReplay::hasReplayFrames() const
{
    return m_frames.size() >= 2;
}

bool InstantReplay::exporting() const
{
    return m_exporting;
}

QString InstantReplay::statusMessage() const
{
    return m_statusMessage;
}

bool InstantReplay::audioBuffered() const
{
    return !m_audioChunks.isEmpty();
}

qint64 InstantReplay::currentTimestampMs() const
{
    return m_clock.elapsed();
}

void InstantReplay::captureFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    const qint64 timestamp = m_clock.elapsed();
    const qint64 minimumInterval = qMax<qint64>(1, 1000 / m_frameRate);
    if (m_lastAcceptedTimestampMs >= 0 && timestamp - m_lastAcceptedTimestampMs < minimumInterval)
        return;
    m_lastAcceptedTimestampMs = timestamp;

    m_pendingFrame = {frame, timestamp};
    startEncodingPendingFrame();
}

void InstantReplay::captureAudioBuffer(const QAudioBuffer &buffer, qint64 timestampUs)
{
    const QAudioFormat format = buffer.format();
    const int bytesPerFrame = format.bytesPerFrame();
    if (!buffer.isValid() || !format.isValid() || bytesPerFrame <= 0 || buffer.frameCount() <= 0)
        return;

    if (m_audioFormat.isValid() && format != m_audioFormat)
        clearAudioBuffer();
    m_audioFormat = format;

    AudioChunk chunk;
    chunk.pcm = QByteArray(buffer.constData<char>(), buffer.byteCount());
    chunk.format = format;
    chunk.timestampUs = qMax<qint64>(0, timestampUs);
    chunk.frameCount = buffer.frameCount();
    if (chunk.pcm.isEmpty())
        return;

    const bool hadAudio = !m_audioChunks.isEmpty();
    m_audioBufferBytes += chunk.pcm.size();
    m_audioChunks.append(std::move(chunk));
    trimBuffer();
    if (hadAudio != !m_audioChunks.isEmpty())
        emit audioBufferedChanged();
}

void InstantReplay::clearAudioBuffer()
{
    const bool hadAudio = !m_audioChunks.isEmpty();
    m_audioChunks.clear();
    m_audioBufferBytes = 0;
    m_audioFormat = {};
    if (hadAudio)
        emit audioBufferedChanged();
}

void InstantReplay::setFrameRate(int frameRate)
{
    m_frameRate = qBound(15, frameRate, 60);
}

bool InstantReplay::saveLastClip(const QString &outputPath, int durationSeconds,
                                 int frameRate, int quality)
{
    if (m_exporting) {
        emit operationFailed(QStringLiteral("A replay clip is already being exported."));
        return false;
    }
    if (m_frames.size() < 2) {
        emit operationFailed(QStringLiteral("Wait for the replay buffer to collect a few seconds of video."));
        return false;
    }

    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    const bool includeAudio = !m_audioChunks.isEmpty();
    if (includeAudio)
        format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
#ifndef Q_OS_LINUX
    if (!format.isSupported(QMediaFormat::Encode)) {
        emit operationFailed(includeAudio
                                 ? QStringLiteral("This system has no Qt Multimedia encoder for MP4/H.264/AAC.")
                                 : QStringLiteral("This system has no Qt Multimedia encoder for MP4/H.264."));
        return false;
    }
#endif

    m_outputPath = outputPath;
    m_exportTemporaryDirectory = QDir(QDir::tempPath()).filePath(
        QStringLiteral("moment-replay-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    m_pendingExportQuality = qBound(0, quality, 4);
    m_pendingExportFrameRate = qBound(15, frameRate, 60);
    const qint64 endTimestamp = m_frames.constLast().timestampMs;
    m_pendingExportStartMs = qMax<qint64>(0, endTimestamp - qMax(1, durationSeconds) * 1000LL);
    const QVector<Frame> frameSnapshot = m_frames;
    const QVector<AudioChunk> audioSnapshot = m_audioChunks;
    const QAudioFormat audioFormatSnapshot = m_audioFormat;
    m_finalizeRequested = false;
    m_exporting = true;
    emit exportingChanged();
    setStatusMessage(QStringLiteral("Preparing replay clip…"));
    m_exportPreparation.setFuture(QtConcurrent::run([frameSnapshot, audioSnapshot, audioFormatSnapshot,
                                                       start = m_pendingExportStartMs, end = endTimestamp,
                                                       temporaryDirectory = m_exportTemporaryDirectory] {
        return prepareExport(frameSnapshot, audioSnapshot, audioFormatSnapshot, start, end,
                             temporaryDirectory);
    }));
    return true;
}

InstantReplay::PreparedExport InstantReplay::prepareExport(QVector<Frame> frames,
                                                           QVector<AudioChunk> audioChunks,
                                                           QAudioFormat audioFormat,
                                                           qint64 startTimestampMs,
                                                           qint64 endTimestampMs,
                                                           QString temporaryDirectory)
{
    PreparedExport prepared;
    prepared.audioFormat = audioFormat;
    prepared.frameDirectory = temporaryDirectory;
    prepared.audioWavePath = QDir(temporaryDirectory).filePath(QStringLiteral("audio.wav"));
    for (Frame &frame : frames) {
        if (frame.timestampMs >= startTimestampMs)
            prepared.frames.append(std::move(frame));
    }
    if (prepared.frames.size() < 2) {
        prepared.error = QStringLiteral("The selected replay window does not contain enough frames yet.");
        return prepared;
    }
    prepared.firstImage = QImage::fromData(prepared.frames.constFirst().jpeg, "JPEG");
    if (prepared.firstImage.isNull()) {
        prepared.error = QStringLiteral("The replay buffer contains an unreadable frame.");
        return prepared;
    }
#ifdef Q_OS_LINUX
    if (!QDir().mkpath(temporaryDirectory)) {
        prepared.error = QStringLiteral("Could not create temporary replay files.");
        return prepared;
    }
    for (qsizetype i = 0; i < prepared.frames.size(); ++i) {
        const QString framePath = QDir(temporaryDirectory).filePath(
            QStringLiteral("frame_%1.jpg").arg(i, 6, 10, QLatin1Char('0')));
        QFile frameFile(framePath);
        if (!frameFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || frameFile.write(prepared.frames.at(i).jpeg) != prepared.frames.at(i).jpeg.size()) {
            prepared.error = QStringLiteral("Could not prepare replay frames for export.");
            return prepared;
        }
    }
#endif

    const qint64 startUs = startTimestampMs * 1000;
    const qint64 endUs = endTimestampMs * 1000;
    const int sampleRate = audioFormat.sampleRate();
    const int bytesPerFrame = audioFormat.bytesPerFrame();
    if (sampleRate <= 0 || bytesPerFrame <= 0)
        return prepared;
    for (const AudioChunk &chunk : audioChunks) {
        if (chunk.format != audioFormat || chunk.frameCount <= 0)
            continue;
        const qint64 durationUs = qint64(chunk.frameCount) * 1000000 / sampleRate;
        const qint64 chunkEndUs = chunk.timestampUs + durationUs;
        if (chunkEndUs <= startUs || chunk.timestampUs >= endUs)
            continue;
        const qint64 leadingUs = qMax<qint64>(0, startUs - chunk.timestampUs);
        const qint64 trailingUs = qMax<qint64>(0, chunkEndUs - endUs);
        const int firstFrame = qBound(0, static_cast<int>((leadingUs * sampleRate + 999999) / 1000000), chunk.frameCount);
        const int framesAfterEnd = qBound(0, static_cast<int>((trailingUs * sampleRate) / 1000000), chunk.frameCount - firstFrame);
        const int framesToSend = chunk.frameCount - firstFrame - framesAfterEnd;
        if (framesToSend <= 0)
            continue;
        const qsizetype byteOffset = qsizetype(firstFrame) * bytesPerFrame;
        const qsizetype byteCount = qsizetype(framesToSend) * bytesPerFrame;
        const qint64 chunkStartUs = chunk.timestampUs + qint64(firstFrame) * 1000000 / sampleRate;
        const qint64 presentationUs = qMax<qint64>(0, chunkStartUs - startUs);
        const QAudioBuffer cropped(chunk.pcm.mid(byteOffset, byteCount), audioFormat, presentationUs);
        if (cropped.isValid())
            prepared.audioBuffers.append(cropped);
    }
#ifdef Q_OS_LINUX
    if (!prepared.audioBuffers.isEmpty()
        && !writePcmWave(prepared.audioWavePath, prepared.audioBuffers, audioFormat,
                         (endTimestampMs - startTimestampMs) * 1000, &prepared.error))
        return prepared;
#endif
    return prepared;
}

void InstantReplay::startPreparedExport(PreparedExport prepared)
{
    if (!m_exporting)
        return;
    if (!prepared.error.isEmpty()) {
        failExport(prepared.error);
        return;
    }
    if (!QDir().mkpath(QFileInfo(m_outputPath).absolutePath())) {
        failExport(QStringLiteral("Could not create the clip output directory."));
        return;
    }
#ifdef Q_OS_LINUX
    const QString ffmpeg = momentMediaTool(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        failExport(QStringLiteral("FFmpeg is required to export replay clips on Linux."));
        return;
    }
    m_partialOutputPath = m_outputPath + QStringLiteral(".partial.mp4");
    QFile::remove(m_partialOutputPath);
    const QString imagePattern = QDir(prepared.frameDirectory).filePath(QStringLiteral("frame_%06d.jpg"));
    QStringList arguments{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-y"), QStringLiteral("-re"), QStringLiteral("-framerate"),
        QString::number(m_pendingExportFrameRate), QStringLiteral("-threads:v"), QStringLiteral("1"),
        QStringLiteral("-i"), imagePattern
    };
    if (!prepared.audioBuffers.isEmpty()) {
        arguments << QStringLiteral("-re") << QStringLiteral("-i") << prepared.audioWavePath;
    }
    arguments << QStringLiteral("-map") << QStringLiteral("0:v:0");
    if (!prepared.audioBuffers.isEmpty())
        arguments << QStringLiteral("-map") << QStringLiteral("1:a:0");
    else
        arguments << QStringLiteral("-an");

    const int bitrate = MomentEncoding::videoBitRate(m_pendingExportQuality, prepared.firstImage.size());
    // libx264 cannot encode frames whose width or height is not divisible by 2
    // with the yuv420p pixel format; scale to even dimensions before encoding.
    // Fit the source into the output bounds, then round both final axes down
    // to even values. This keeps libx264/yuv420p valid for odd and ultrawide
    // desktop sizes even if the scaler's aspect-ratio rounding is fractional.
    arguments << QStringLiteral("-vf") << QStringLiteral("scale=w='min(1920,iw)':h='min(1080,ih)':force_original_aspect_ratio=decrease,scale=w='max(2,trunc(iw/2)*2)':h='max(2,trunc(ih/2)*2)',setsar=1")
              << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-threads:v") << QStringLiteral("1")
              << QStringLiteral("-preset") << QStringLiteral("ultrafast")
              << QStringLiteral("-tune") << QStringLiteral("zerolatency")
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
              << QStringLiteral("-b:v") << QString::number(bitrate)
              << QStringLiteral("-maxrate") << QString::number(bitrate)
              << QStringLiteral("-bufsize") << QString::number(bitrate * 2)
              << QStringLiteral("-g") << QString::number(m_pendingExportFrameRate);
    if (!prepared.audioBuffers.isEmpty())
        arguments << QStringLiteral("-c:a") << QStringLiteral("aac")
                  << QStringLiteral("-b:a") << QStringLiteral("128k");
    arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << m_partialOutputPath;

    setStatusMessage(QStringLiteral("Saving replay…"));
    m_exportProcess.setProgram(ffmpeg);
    m_exportProcess.setArguments(arguments);
    m_exportProcess.setWorkingDirectory(prepared.frameDirectory);
    m_exportProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_exportProcess.start();
    return;
#endif
    m_exportFrames = std::move(prepared.frames);
    m_exportAudioBuffers = std::move(prepared.audioBuffers);
    delete m_exportInput;
    const QVideoFrame firstFrame(prepared.firstImage);
    m_exportInput = new QVideoFrameInput(firstFrame.surfaceFormat(), this);
    m_exportSession.setVideoFrameInput(m_exportInput);

    delete m_exportAudioInput;
    m_exportAudioInput = nullptr;
    if (!m_exportAudioBuffers.isEmpty()) {
        m_exportAudioInput = new QAudioBufferInput(prepared.audioFormat, this);
        m_exportSession.setAudioBufferInput(m_exportAudioInput);
        connect(m_exportAudioInput, &QAudioBufferInput::readyToSendAudioBuffer,
                this, &InstantReplay::sendExportAudio, Qt::UniqueConnection);
    } else {
        m_exportSession.setAudioBufferInput(nullptr);
    }

    m_exportIndex = 0;
    m_audioExportIndex = 0;
    m_pendingExportImage = {};
    m_decodingExportFrame = false;
    m_videoInputFinished = false;
    m_audioInputFinished = m_exportAudioBuffers.isEmpty();
    m_exportFps = m_pendingExportFrameRate;
    m_exportOriginMs = m_pendingExportStartMs;
    setStatusMessage(QStringLiteral("Encoding replay clip…"));
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    if (m_exportAudioInput)
        format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    m_exportRecorder.setMediaFormat(format);
    m_exportRecorder.setVideoFrameRate(m_exportFps);
    m_exportRecorder.setQuality(static_cast<QMediaRecorder::Quality>(m_pendingExportQuality));
    m_exportRecorder.setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
    m_exportRecorder.setVideoBitRate(MomentEncoding::videoBitRate(m_pendingExportQuality, prepared.firstImage.size()));
    if (m_exportAudioInput)
        m_exportRecorder.setAudioBitRate(128000);
    m_exportRecorder.setOutputLocation(QUrl::fromLocalFile(m_outputPath));
    m_exportRecorder.record();
}

void InstantReplay::setBufferSeconds(int seconds)
{
    const int bounded = qBound(5, seconds, 120);
    if (m_bufferSeconds == bounded)
        return;
    m_bufferSeconds = bounded;
    trimBuffer();
    emit bufferSecondsChanged();
    emit bufferedSecondsChanged();
}

InstantReplay::Frame InstantReplay::encodeFrame(const QVideoFrame &frame, qint64 timestampMs)
{
    Frame encoded;
    encoded.timestampMs = timestampMs;
    QImage image = frame.toImage();
    if (image.isNull())
        return encoded;
    const QSize boundedSize = MomentEncoding::replayFrameSize(image.size());
    if (image.size() != boundedSize)
        image = image.scaled(boundedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QBuffer buffer(&encoded.jpeg);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(48);
    if (!writer.write(image))
        return {};
    encoded.size = image.size();
    return encoded;
}

void InstantReplay::startEncodingPendingFrame()
{
    if (m_encodingFrame || !m_pendingFrame.frame.isValid())
        return;
    const PendingFrame pending = std::move(m_pendingFrame);
    m_pendingFrame = {};
    m_encodingFrame = true;
    m_frameEncoder.setFuture(QtConcurrent::run([frame = pending.frame, timestamp = pending.timestampMs] {
        return encodeFrame(frame, timestamp);
    }));
}

void InstantReplay::appendEncodedFrame(Frame frame)
{
    const qsizetype bytes = frame.jpeg.size();
    m_frames.append(std::move(frame));
    m_bufferBytes += bytes;
    trimBuffer();
    emit bufferedSecondsChanged();
    setStatusMessage(QStringLiteral("Replay buffer ready · %1s available").arg(bufferedSeconds()));
}

void InstantReplay::trimBuffer()
{
    const bool hadAudio = !m_audioChunks.isEmpty();
    const qint64 latestVideo = m_frames.isEmpty() ? -1 : m_frames.constLast().timestampMs;
    const qint64 latestAudio = m_audioChunks.isEmpty()
        ? -1
        : m_audioChunks.constLast().timestampUs / 1000
            + qint64(m_audioChunks.constLast().frameCount) * 1000
                / qMax(1, m_audioChunks.constLast().format.sampleRate());
    const qint64 latest = qMax(latestVideo, latestAudio);
    if (latest < 0)
        return;
    const qint64 cutoff = latest - m_bufferSeconds * 1000LL;
    while (!m_frames.isEmpty()
           && (m_frames.constFirst().timestampMs < cutoff || m_bufferBytes > m_maxBufferBytes)) {
        m_bufferBytes -= m_frames.constFirst().jpeg.size();
        m_frames.removeFirst();
    }
    while (!m_audioChunks.isEmpty()
           && (m_audioChunks.constFirst().timestampUs / 1000 < cutoff
               || m_audioBufferBytes > m_maxAudioBufferBytes)) {
        m_audioBufferBytes -= m_audioChunks.constFirst().pcm.size();
        m_audioChunks.removeFirst();
    }
    if (hadAudio != !m_audioChunks.isEmpty())
        emit audioBufferedChanged();
}

void InstantReplay::sendExportFrames()
{
    if (!m_exporting || m_exportRecorder.recorderState() != QMediaRecorder::RecordingState)
        return;

    if (m_exportIndex < m_exportFrames.size()) {
        if (m_pendingExportImage.isNull()) {
            if (!m_decodingExportFrame) {
                m_decodingExportFrame = true;
                const QByteArray jpeg = m_exportFrames.at(m_exportIndex).jpeg;
                m_exportFrameDecoder.setFuture(QtConcurrent::run([jpeg] {
                    return QImage::fromData(jpeg, "JPEG");
                }));
            }
            return;
        }
        const Frame &stored = m_exportFrames.at(m_exportIndex);
        QVideoFrame frame(m_pendingExportImage);
        frame.setStartTime((stored.timestampMs - m_exportOriginMs) * 1000);
        const qint64 durationUs = qMax<qint64>(1, 1000000 / m_exportFps);
        frame.setEndTime(frame.startTime() + durationUs);
        if (!m_exportInput->sendVideoFrame(frame))
            return;
        m_pendingExportImage = {};
        ++m_exportIndex;
    }

    if (m_exportIndex >= m_exportFrames.size()) {
        m_videoInputFinished = true;
        maybeFinishExport();
    }
}

void InstantReplay::startExportPump()
{
    if (!m_exporting || m_exportRecorder.recorderState() != QMediaRecorder::RecordingState)
        return;
    // Feed at playback speed instead of in a tight loop. The export takes
    // about the clip's duration, but avoids a sudden CPU spike on F8.
    m_exportPump.start(qMax(1, 1000 / qMax(1, m_exportFps)));
    if (!m_audioInputFinished)
        m_audioExportPump.start();
}

void InstantReplay::sendExportAudio()
{
    if (!m_exporting || !m_exportAudioInput
        || m_exportRecorder.recorderState() != QMediaRecorder::RecordingState)
        return;

    if (m_audioExportIndex < m_exportAudioBuffers.size()) {
        if (!m_exportAudioInput->sendAudioBuffer(m_exportAudioBuffers.at(m_audioExportIndex)))
            return;
        ++m_audioExportIndex;
    }
    if (m_audioExportIndex >= m_exportAudioBuffers.size()) {
        m_audioExportPump.stop();
        m_audioInputFinished = true;
        maybeFinishExport();
    }
}

void InstantReplay::maybeFinishExport()
{
    if (!m_exporting || !m_videoInputFinished || !m_audioInputFinished)
        return;
    m_finalizeRequested = true;
    m_exportRecorder.stop();
}

void InstantReplay::finishExport()
{
    if (!m_exporting || !m_finalizeRequested)
        return;
    m_finalizeRequested = false;
    m_exportPump.stop();
    m_audioExportPump.stop();
    const QFileInfo output(m_outputPath);
    if (!output.exists() || output.size() == 0) {
        QFile::remove(m_outputPath);
        failExport(QStringLiteral("The replay export stopped without creating a video file."));
        return;
    }

    const QString savedPath = m_outputPath;
    m_exporting = false;
    emit exportingChanged();
    setStatusMessage(QStringLiteral("Replay clip saved"));
    m_exportFrames.clear();
    m_exportAudioBuffers.clear();
    if (!m_exportTemporaryDirectory.isEmpty())
        QDir(m_exportTemporaryDirectory).removeRecursively();
    m_exportTemporaryDirectory.clear();
    m_exportSession.setVideoFrameInput(nullptr);
    m_exportSession.setAudioBufferInput(nullptr);
    delete m_exportInput;
    m_exportInput = nullptr;
    delete m_exportAudioInput;
    m_exportAudioInput = nullptr;
    emit clipSaved(savedPath);
}

void InstantReplay::finishExternalExport(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_exporting)
        return;
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        QString details = QString::fromLocal8Bit(m_exportProcess.readAllStandardError()).trimmed();
        if (details.isEmpty())
            details = QStringLiteral("FFmpeg exited with code %1.").arg(exitCode);
        failExport(details);
        return;
    }

    const QFileInfo partial(m_partialOutputPath);
    if (!partial.exists() || partial.size() <= 0 || !QFile::rename(m_partialOutputPath, m_outputPath)) {
        failExport(QStringLiteral("FFmpeg finished, but Moment could not finalize the replay file."));
        return;
    }
    QDir(m_exportTemporaryDirectory).removeRecursively();
    m_exportTemporaryDirectory.clear();
    m_partialOutputPath.clear();
    const QString savedPath = m_outputPath;
    m_exporting = false;
    emit exportingChanged();
    setStatusMessage(QStringLiteral("Replay clip saved"));
    emit clipSaved(savedPath);
}

void InstantReplay::failExport(const QString &message)
{
    m_exporting = false;
    if (m_exportProcess.state() != QProcess::NotRunning) {
        m_exportProcess.kill();
        m_exportProcess.waitForFinished(1000);
    }
    m_exportPump.stop();
    m_audioExportPump.stop();
    if (m_exportRecorder.recorderState() != QMediaRecorder::StoppedState)
        m_exportRecorder.stop();
    QFile::remove(m_outputPath);
    if (!m_partialOutputPath.isEmpty())
        QFile::remove(m_partialOutputPath);
    if (!m_exportTemporaryDirectory.isEmpty())
        QDir(m_exportTemporaryDirectory).removeRecursively();
    m_partialOutputPath.clear();
    m_exportTemporaryDirectory.clear();
    m_finalizeRequested = false;
    emit exportingChanged();
    m_exportFrames.clear();
    m_exportAudioBuffers.clear();
    if (m_exportInput) {
        m_exportSession.setVideoFrameInput(nullptr);
        delete m_exportInput;
        m_exportInput = nullptr;
    }
    if (m_exportAudioInput) {
        m_exportSession.setAudioBufferInput(nullptr);
        delete m_exportAudioInput;
        m_exportAudioInput = nullptr;
    }
    setStatusMessage(message);
    emit operationFailed(message);
}

void InstantReplay::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}
