#include "instant_replay.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QMediaFormat>
#include <QVideoFrameFormat>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

InstantReplay::InstantReplay(QObject *parent)
    : QObject(parent), m_exportRecorder(this)
{
    m_clock.start();
    m_exportSession.setRecorder(&m_exportRecorder);

    connect(&m_frameEncoder, &QFutureWatcher<Frame>::finished, this, [this] {
        Frame frame = m_frameEncoder.result();
        m_encodingFrame = false;
        if (!frame.jpeg.isEmpty())
            appendEncodedFrame(std::move(frame));
        startEncodingPendingFrame();
    });

    connect(&m_exportRecorder, &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::RecordingState)
            sendExportFrames();
        else if (state == QMediaRecorder::StoppedState && m_finalizeRequested)
            finishExport();
    });
    connect(&m_exportRecorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &message) {
        failExport(message.isEmpty() ? QStringLiteral("Clip encoding failed.") : message);
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
    if (!format.isSupported(QMediaFormat::Encode)) {
        emit operationFailed(includeAudio
                                 ? QStringLiteral("This system has no Qt Multimedia encoder for MP4/H.264/AAC.")
                                 : QStringLiteral("This system has no Qt Multimedia encoder for MP4/H.264."));
        return false;
    }

    const qint64 endTimestamp = m_frames.constLast().timestampMs;
    const qint64 startTimestamp = qMax<qint64>(0, endTimestamp - qMax(1, durationSeconds) * 1000LL);
    const qint64 startTimestampUs = startTimestamp * 1000;
    const qint64 endTimestampUs = endTimestamp * 1000;
    const int sampleRate = m_audioFormat.sampleRate();
    const int bytesPerFrame = m_audioFormat.bytesPerFrame();
    m_exportAudioBuffers.clear();
    if (includeAudio && sampleRate > 0 && bytesPerFrame > 0) {
        for (const AudioChunk &chunk : m_audioChunks) {
            if (chunk.format != m_audioFormat || chunk.frameCount <= 0)
                continue;
            const qint64 durationUs = qint64(chunk.frameCount) * 1000000 / sampleRate;
            const qint64 chunkEndUs = chunk.timestampUs + durationUs;
            if (chunkEndUs <= startTimestampUs || chunk.timestampUs >= endTimestampUs)
                continue;

            const qint64 leadingUs = qMax<qint64>(0, startTimestampUs - chunk.timestampUs);
            const qint64 trailingUs = qMax<qint64>(0, chunkEndUs - endTimestampUs);
            const int firstFrame = qBound(0, static_cast<int>((leadingUs * sampleRate + 999999) / 1000000), chunk.frameCount);
            const int framesAfterEnd = qBound(0, static_cast<int>((trailingUs * sampleRate) / 1000000), chunk.frameCount - firstFrame);
            const int framesToSend = chunk.frameCount - firstFrame - framesAfterEnd;
            if (framesToSend <= 0)
                continue;

            const qsizetype byteOffset = qsizetype(firstFrame) * bytesPerFrame;
            const qsizetype byteCount = qsizetype(framesToSend) * bytesPerFrame;
            const qint64 chunkStartUs = chunk.timestampUs + qint64(firstFrame) * 1000000 / sampleRate;
            const qint64 presentationUs = qMax<qint64>(0, chunkStartUs - startTimestampUs);
            const QByteArray data = chunk.pcm.mid(byteOffset, byteCount);
            const QAudioBuffer cropped(data, m_audioFormat, presentationUs);
            if (cropped.isValid())
                m_exportAudioBuffers.append(cropped);
        }
    }
    m_exportFrames.clear();
    for (const Frame &frame : m_frames) {
        if (frame.timestampMs >= startTimestamp)
            m_exportFrames.append(frame);
    }
    if (m_exportFrames.size() < 2) {
        emit operationFailed(QStringLiteral("The selected replay window does not contain enough frames yet."));
        return false;
    }

    const QImage firstImage = QImage::fromData(m_exportFrames.constFirst().jpeg, "JPEG");
    if (firstImage.isNull()) {
        emit operationFailed(QStringLiteral("The replay buffer contains an unreadable frame."));
        return false;
    }

    if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) {
        emit operationFailed(QStringLiteral("Could not create the clip output directory."));
        return false;
    }

    delete m_exportInput;
    const QVideoFrame firstFrame(firstImage);
    m_exportInput = new QVideoFrameInput(firstFrame.surfaceFormat(), this);
    m_exportSession.setVideoFrameInput(m_exportInput);
    connect(m_exportInput, &QVideoFrameInput::readyToSendVideoFrame,
            this, &InstantReplay::sendExportFrames, Qt::UniqueConnection);

    delete m_exportAudioInput;
    m_exportAudioInput = nullptr;
    if (!m_exportAudioBuffers.isEmpty()) {
        m_exportAudioInput = new QAudioBufferInput(m_audioFormat, this);
        m_exportSession.setAudioBufferInput(m_exportAudioInput);
        connect(m_exportAudioInput, &QAudioBufferInput::readyToSendAudioBuffer,
                this, &InstantReplay::sendExportAudio, Qt::UniqueConnection);
    } else {
        m_exportSession.setAudioBufferInput(nullptr);
    }

    m_exportIndex = 0;
    m_audioExportIndex = 0;
    m_videoInputFinished = false;
    m_audioInputFinished = m_exportAudioBuffers.isEmpty();
    m_exportFps = qBound(15, frameRate, 60);
    m_exportOriginMs = startTimestamp;
    m_outputPath = outputPath;
    m_finalizeRequested = false;
    m_exporting = true;
    emit exportingChanged();
    setStatusMessage(QStringLiteral("Encoding replay clip…"));

    m_exportRecorder.setMediaFormat(format);
    m_exportRecorder.setVideoFrameRate(m_exportFps);
    m_exportRecorder.setQuality(static_cast<QMediaRecorder::Quality>(qBound(0, quality, 4)));
    if (m_exportAudioInput)
        m_exportRecorder.setAudioBitRate(160000);
    m_exportRecorder.setOutputLocation(QUrl::fromLocalFile(m_outputPath));
    m_exportRecorder.record();
    return true;
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
    const QImage image = frame.toImage();
    if (image.isNull())
        return encoded;

    QBuffer buffer(&encoded.jpeg);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(55);
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

    constexpr int framesPerTurn = 6;
    int sent = 0;
    while (m_exportIndex < m_exportFrames.size() && sent < framesPerTurn) {
        const Frame &stored = m_exportFrames.at(m_exportIndex);
        QImage image = QImage::fromData(stored.jpeg, "JPEG");
        if (image.isNull()) {
            failExport(QStringLiteral("A replay frame could not be decoded."));
            return;
        }

        QVideoFrame frame(image);
        frame.setStartTime((stored.timestampMs - m_exportOriginMs) * 1000);
        const qint64 durationUs = qMax<qint64>(1, 1000000 / m_exportFps);
        frame.setEndTime(frame.startTime() + durationUs);
        if (!m_exportInput->sendVideoFrame(frame))
            return;
        ++m_exportIndex;
        ++sent;
    }

    if (m_exportIndex >= m_exportFrames.size()) {
        m_videoInputFinished = true;
        maybeFinishExport();
    } else {
        QMetaObject::invokeMethod(this, &InstantReplay::sendExportFrames, Qt::QueuedConnection);
    }
}

void InstantReplay::sendExportAudio()
{
    if (!m_exporting || !m_exportAudioInput
        || m_exportRecorder.recorderState() != QMediaRecorder::RecordingState)
        return;

    while (m_audioExportIndex < m_exportAudioBuffers.size()) {
        if (!m_exportAudioInput->sendAudioBuffer(m_exportAudioBuffers.at(m_audioExportIndex)))
            return;
        ++m_audioExportIndex;
    }
    m_audioInputFinished = true;
    maybeFinishExport();
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
    m_exportSession.setVideoFrameInput(nullptr);
    m_exportSession.setAudioBufferInput(nullptr);
    delete m_exportInput;
    m_exportInput = nullptr;
    delete m_exportAudioInput;
    m_exportAudioInput = nullptr;
    emit clipSaved(savedPath);
}

void InstantReplay::failExport(const QString &message)
{
    if (m_exportRecorder.recorderState() != QMediaRecorder::StoppedState)
        m_exportRecorder.stop();
    QFile::remove(m_outputPath);
    m_finalizeRequested = false;
    m_exporting = false;
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
