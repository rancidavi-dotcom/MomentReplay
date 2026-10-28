#include "screen_recorder.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMediaFormat>
#include <QMediaDevices>
#include <QVideoFrameFormat>
#include <QFileInfo>
#include <QtAudio>
#include <QUuid>

ScreenRecorder::ScreenRecorder(ClipLibraryModel &library, RecordingPreferences &preferences,
                               QString outputDirectory, QString screenshotDirectory,
                               QObject *parent)
    : QObject(parent), m_library(library), m_preferences(preferences),
      m_outputDirectory(std::move(outputDirectory)),
      m_screenshotDirectory(std::move(screenshotDirectory)), m_videoSink(this),
      m_mediaRecorder(this), m_mediaDevices(this), m_replayBuffer(this), m_audioPollTimer(this)
{
    initialize();
}

ScreenRecorder::ScreenRecorder(ClipLibraryModel &library, RecordingPreferences &preferences,
                               QString outputDirectory, QString screenshotDirectory,
                               QVideoFrameInput &videoFrameInput,
                               QObject *parent)
    : QObject(parent), m_library(library), m_preferences(preferences),
      m_outputDirectory(std::move(outputDirectory)),
      m_screenshotDirectory(std::move(screenshotDirectory)), m_videoSink(this),
      m_mediaRecorder(this), m_mediaDevices(this), m_replayBuffer(this), m_audioPollTimer(this),
      m_videoFrameInput(&videoFrameInput)
{
    initialize();
    m_captureSession.setVideoFrameInput(m_videoFrameInput);
}

void ScreenRecorder::initialize()
{
    m_audioPollTimer.setInterval(10);
    connect(&m_audioPollTimer, &QTimer::timeout, this, &ScreenRecorder::pollAudioSource);
    connect(&m_mediaDevices, &QMediaDevices::audioInputsChanged,
            this, [this] {
        emit audioDevicesChanged();
        if (m_preferences.captureAudio() && m_captureActive && !recording())
            syncAudioCapture();
    });
    connect(&m_preferences, &RecordingPreferences::captureAudioChanged,
            this, &ScreenRecorder::syncAudioCapture);
    connect(&m_preferences, &RecordingPreferences::audioInputIdChanged,
            this, &ScreenRecorder::syncAudioCapture);
    m_captureSession.setRecorder(&m_mediaRecorder);
    m_captureSession.setVideoSink(&m_videoSink);
    const QString tokenPath = QFileInfo(m_preferences.settingsFilePath()).absolutePath()
        + QStringLiteral("/screencast-restore-token");
    m_waylandCapture = new WaylandScreenCast(tokenPath, this);
    connect(m_waylandCapture, &WaylandScreenCast::frameReady, this,
            [this](const QImage &image, qint64 presentationTimeUs) {
        if (image.isNull())
            return;
        updateLatestFrame(image);

        if (!m_portalFrameInput || m_portalFrameInput->format().frameSize() != image.size()) {
            m_captureSession.setVideoFrameInput(nullptr);
            const QVideoFrameFormat format(image.size(), QVideoFrameFormat::Format_ARGB8888);
            m_portalFrameInput = std::make_unique<QVideoFrameInput>(format, this);
            m_captureSession.setVideoFrameInput(m_portalFrameInput.get());
            connect(m_portalFrameInput.get(), &QVideoFrameInput::readyToSendVideoFrame,
                    this, [this] {
                if (!m_hasPendingPortalFrame || !m_portalFrameInput)
                    return;
                if (m_portalFrameInput->sendVideoFrame(m_pendingPortalFrame))
                    m_hasPendingPortalFrame = false;
            });
        }

        QVideoFrame frame(image);
        if (presentationTimeUs >= 0) {
            frame.setStartTime(presentationTimeUs);
            frame.setEndTime(presentationTimeUs + 33333);
        }
        m_replayBuffer.captureFrame(frame);
        if (m_portalFrameInput->sendVideoFrame(frame)) {
            m_hasPendingPortalFrame = false;
        } else {
            m_pendingPortalFrame = frame;
            m_hasPendingPortalFrame = true;
        }

        if (m_recordAfterCapture && m_waylandCapture->active()) {
            m_recordAfterCapture = false;
            m_mediaRecorder.record();
        }
    });
    connect(m_waylandCapture, &WaylandScreenCast::activeChanged, this, [this](bool active) {
        m_captureActive = active;
        if (active && m_preferences.captureAudio())
            startAudioCapture();
        else if (!active)
            stopAudioCapture();
        if (!active || m_state != State::AwaitingScreenPermission)
            return;
        if (!m_recordAfterCapture) {
            m_state = State::Buffering;
            emit stateChanged();
            setStatusMessage(QStringLiteral("Replay buffer is capturing your screen"));
        }
    });
    connect(m_waylandCapture, &WaylandScreenCast::failed,
            this, [this](const QString &message) { fail(message); });
    m_replayBuffer.setBufferSeconds(m_preferences.replaySeconds());
    connect(&m_preferences, &RecordingPreferences::replaySecondsChanged, this, [this] {
        m_replayBuffer.setBufferSeconds(m_preferences.replaySeconds());
    });
    connect(&m_replayBuffer, &InstantReplay::bufferSecondsChanged,
            this, &ScreenRecorder::bufferSecondsChanged);

    connect(&m_videoSink, &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame &frame) {
        if (!m_waylandCapture || !m_waylandCapture->active()) {
            const QImage image = frame.toImage();
            if (!image.isNull())
                updateLatestFrame(image);
            m_replayBuffer.captureFrame(frame);
        }
    });
    connect(&m_replayBuffer, &InstantReplay::bufferedSecondsChanged,
            this, &ScreenRecorder::bufferedSecondsChanged);
    connect(&m_replayBuffer, &InstantReplay::exportingChanged, this, &ScreenRecorder::stateChanged);
    connect(&m_replayBuffer, &InstantReplay::statusMessageChanged, this, [this] {
        if (!m_replayBuffer.statusMessage().isEmpty())
            setStatusMessage(m_replayBuffer.statusMessage());
    });
    connect(&m_replayBuffer, &InstantReplay::clipSaved, this, [this](const QString &path) {
        m_library.importFile(path);
        setStatusMessage(QStringLiteral("Replay clip saved and added to your library"));
        emit replayClipSaved(path);
    });
    connect(&m_replayBuffer, &InstantReplay::operationFailed,
            this, &ScreenRecorder::operationFailed);

    connect(&m_mediaRecorder, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &message) {
        fail(message.isEmpty() ? QStringLiteral("Video encoding failed.") : message);
    });
    connect(&m_mediaRecorder, &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::RecordingState) {
            m_state = State::Recording;
            m_sessionAudioOriginUs = m_replayBuffer.currentTimestampMs() * 1000;
            m_sessionAudioQueue.clear();
            m_sessionAudioQueuedBytes = 0;
            emit stateChanged();
            setStatusMessage(m_videoFrameInput
                                 ? QStringLiteral("Recording test video")
                                 : QStringLiteral("Recording screen to %1")
                                       .arg(QFileInfo(m_outputPath).fileName()));
        }
        if (state == QMediaRecorder::StoppedState && m_stopRequested)
            finalizeRecording();
    });
}

bool ScreenRecorder::busy() const
{
    return m_state != State::Idle;
}

bool ScreenRecorder::recording() const
{
    return m_state == State::Recording;
}

bool ScreenRecorder::buffering() const
{
    return m_state == State::Buffering || m_state == State::Recording;
}

int ScreenRecorder::bufferedSeconds() const
{
    return m_replayBuffer.bufferedSeconds();
}

int ScreenRecorder::bufferSeconds() const
{
    return m_replayBuffer.bufferSeconds();
}

void ScreenRecorder::setBufferSeconds(int seconds)
{
    m_preferences.setReplaySeconds(seconds);
}

bool ScreenRecorder::replayExporting() const
{
    return m_replayBuffer.exporting();
}

bool ScreenRecorder::frameAvailable() const
{
    return !m_latestFrame.isNull();
}

QString ScreenRecorder::statusMessage() const
{
    return m_statusMessage;
}

QStringList ScreenRecorder::audioInputNames() const
{
    QStringList names;
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices)
        names.append(device.description());
    return names;
}

int ScreenRecorder::selectedAudioInputIndex() const
{
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    const QByteArray savedId = m_preferences.audioInputId();
    for (qsizetype i = 0; i < devices.size(); ++i) {
        if (devices.at(i).id() == savedId)
            return static_cast<int>(i);
    }
    const QByteArray defaultId = QMediaDevices::defaultAudioInput().id();
    for (qsizetype i = 0; i < devices.size(); ++i) {
        if (devices.at(i).id() == defaultId)
            return static_cast<int>(i);
    }
    return devices.isEmpty() ? -1 : 0;
}

QString ScreenRecorder::audioCaptureStatus() const
{
    return m_audioCaptureStatus;
}

bool ScreenRecorder::audioCaptureActive() const
{
    return bool(m_audioSource) && m_audioSource->state() == QAudio::ActiveState;
}

QAudioDevice ScreenRecorder::selectedAudioDevice() const
{
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    const QByteArray savedId = m_preferences.audioInputId();
    for (const QAudioDevice &device : devices) {
        if (device.id() == savedId)
            return device;
    }
    return QMediaDevices::defaultAudioInput();
}

void ScreenRecorder::setAudioInputIndex(int index)
{
    if (recording())
        return;
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    if (index < 0 || index >= devices.size())
        return;
    stopAudioCapture();
    m_replayBuffer.clearAudioBuffer();
    m_preferences.setAudioInputId(devices.at(index).id());
    if (m_preferences.captureAudio() && m_captureActive)
        startAudioCapture();
    emit audioDevicesChanged();
}

void ScreenRecorder::syncAudioCapture()
{
    if (!m_preferences.captureAudio()) {
        stopAudioCapture();
        m_replayBuffer.clearAudioBuffer();
        setAudioCaptureStatus(QStringLiteral("Audio capture is off"));
        return;
    }
    if (!m_captureActive) {
        setAudioCaptureStatus(QStringLiteral("Audio will start with screen capture"));
        return;
    }
    if (recording()) {
        setAudioCaptureStatus(QStringLiteral("Audio input changes apply after recording stops"));
        return;
    }
    startAudioCapture();
}

void ScreenRecorder::startAudioCapture()
{
    if (!m_preferences.captureAudio() || !m_captureActive || m_audioSource)
        return;

    const QAudioDevice device = selectedAudioDevice();
    if (device.isNull()) {
        setAudioCaptureStatus(QStringLiteral("No audio input is available"));
        if (m_state == State::Recording || m_recordAfterCapture)
            fail(QStringLiteral("Audio capture is enabled, but no audio input device is available."));
        return;
    }

    m_audioFormat = device.preferredFormat();
    if (!m_audioFormat.isValid() || m_audioFormat.bytesPerFrame() <= 0
        || m_audioFormat.sampleRate() <= 0) {
        setAudioCaptureStatus(QStringLiteral("Selected audio input has no supported PCM format"));
        if (m_state == State::Recording || m_recordAfterCapture)
            fail(QStringLiteral("The selected audio device does not provide a supported PCM format."));
        return;
    }

    if (!m_sessionAudioInput || m_sessionAudioInput->format() != m_audioFormat) {
        if (m_sessionAudioInput)
            m_captureSession.setAudioBufferInput(nullptr);
        m_sessionAudioInput = std::make_unique<QAudioBufferInput>(m_audioFormat, this);
        m_captureSession.setAudioBufferInput(m_sessionAudioInput.get());
        connect(m_sessionAudioInput.get(), &QAudioBufferInput::readyToSendAudioBuffer,
                this, &ScreenRecorder::pumpSessionAudio);
    }

    m_audioSource = std::make_unique<QAudioSource>(device, m_audioFormat, this);
    m_audioSource->setBufferFrameCount(qMax(1, m_audioFormat.sampleRate() / 5));
    connect(m_audioSource.get(), &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::StoppedState && m_audioSource && m_audioSource->error() != QtAudio::NoError) {
            const QString message = QStringLiteral("Audio input stopped: %1")
                .arg(static_cast<int>(m_audioSource->error()));
            setAudioCaptureStatus(message);
            if (recording())
                fail(message);
        }
    });
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        const QString message = QStringLiteral("Could not start the selected audio input.");
        m_audioSource.reset();
        m_captureSession.setAudioBufferInput(nullptr);
        m_sessionAudioInput.reset();
        setAudioCaptureStatus(message);
        if (m_state == State::Recording || m_recordAfterCapture)
            fail(message);
        return;
    }
    m_audioOriginMs = m_replayBuffer.currentTimestampMs();
    m_audioFramesCaptured = 0;
    m_sessionAudioEnabled = true;
    m_audioPollTimer.start();
    setAudioCaptureStatus(QStringLiteral("Capturing audio from %1").arg(device.description()));
}

void ScreenRecorder::stopAudioCapture()
{
    m_audioPollTimer.stop();
    m_audioDevice = nullptr;
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
    if (m_sessionAudioInput)
        m_captureSession.setAudioBufferInput(nullptr);
    m_sessionAudioInput.reset();
    m_sessionAudioQueue.clear();
    m_sessionAudioQueuedBytes = 0;
    m_sessionAudioEnabled = false;
    setAudioCaptureStatus(m_preferences.captureAudio()
        ? QStringLiteral("Audio is waiting for screen capture")
        : QStringLiteral("Audio capture is off"));
}

void ScreenRecorder::pollAudioSource()
{
    if (!m_audioDevice || !m_audioSource)
        return;
    const qsizetype bytesPerFrame = m_audioFormat.bytesPerFrame();
    if (bytesPerFrame <= 0)
        return;
    QByteArray data = m_audioDevice->read(m_audioDevice->bytesAvailable());
    const qsizetype usableBytes = data.size() - data.size() % bytesPerFrame;
    if (usableBytes <= 0)
        return;
    data.truncate(usableBytes);
    const QAudioBuffer buffer(data, m_audioFormat,
        m_audioFramesCaptured * 1000000 / qMax(1, m_audioFormat.sampleRate()));
    m_audioFramesCaptured += buffer.frameCount();
    const qint64 timestampUs = m_audioOriginMs * 1000 + buffer.startTime();
    m_replayBuffer.captureAudioBuffer(buffer, timestampUs);
    if (recording())
        queueSessionAudio(buffer, timestampUs);
}

void ScreenRecorder::queueSessionAudio(const QAudioBuffer &buffer, qint64 timestampUs)
{
    if (!m_sessionAudioEnabled || !m_sessionAudioInput || !recording()
        || timestampUs < m_sessionAudioOriginUs)
        return;
    const QByteArray pcm(buffer.constData<char>(), buffer.byteCount());
    const qint64 relativeUs = timestampUs - m_sessionAudioOriginUs;
    QAudioBuffer timedBuffer(pcm, m_audioFormat, relativeUs);
    constexpr qsizetype maxQueuedBytes = 4 * 1024 * 1024;
    if (m_sessionAudioQueuedBytes + timedBuffer.byteCount() > maxQueuedBytes) {
        fail(QStringLiteral("Audio encoding fell behind; the recording was stopped to protect memory."));
        return;
    }
    m_sessionAudioQueuedBytes += timedBuffer.byteCount();
    m_sessionAudioQueue.append(std::move(timedBuffer));
    pumpSessionAudio();
}

void ScreenRecorder::pumpSessionAudio()
{
        if (!m_sessionAudioInput || (!recording() && m_state != State::Stopping))
        return;
    while (!m_sessionAudioQueue.isEmpty()) {
        const QAudioBuffer &buffer = m_sessionAudioQueue.constFirst();
        if (!m_sessionAudioInput->sendAudioBuffer(buffer))
            return;
        m_sessionAudioQueuedBytes -= buffer.byteCount();
        m_sessionAudioQueue.removeFirst();
    }
    if (m_state == State::Stopping)
        stopRecorderWhenAudioDrained();
}

void ScreenRecorder::stopRecorderWhenAudioDrained()
{
    if (m_state != State::Stopping || !m_stopRequested)
        return;
    if (!m_sessionAudioQueue.isEmpty()) {
        QTimer::singleShot(10, this, &ScreenRecorder::pumpSessionAudio);
        return;
    }
    m_mediaRecorder.stop();
    if (m_mediaRecorder.recorderState() == QMediaRecorder::StoppedState)
        finalizeRecording();
}

void ScreenRecorder::setAudioCaptureStatus(const QString &message)
{
    if (m_audioCaptureStatus == message)
        return;
    m_audioCaptureStatus = message;
    emit audioCaptureStatusChanged();
}

void ScreenRecorder::startBuffering()
{
    if (busy())
        return;
    m_replayBuffer.setFrameRate(m_preferences.frameRate());
    m_recordAfterCapture = false;
    m_stopCaptureRequested = false;
    m_state = State::AwaitingScreenPermission;
    emit stateChanged();

    if (m_videoFrameInput) {
        m_captureActive = true;
        m_state = State::Buffering;
        if (m_preferences.captureAudio())
            startAudioCapture();
        emit stateChanged();
        setStatusMessage(QStringLiteral("Replay buffer is ready for test frames"));
        return;
    }
    setStatusMessage(QStringLiteral("Requesting screen access from the desktop…"));
    m_waylandCapture->start();
}

void ScreenRecorder::stopCapture()
{
    if (m_state == State::Idle)
        return;
    if (m_state == State::Stopping)
        return;
    if (m_state == State::Recording) {
        m_stopCaptureRequested = true;
        stopRecording();
        return;
    }
    if (m_state == State::AwaitingScreenPermission
        && m_mediaRecorder.recorderState() == QMediaRecorder::StoppedState) {
        m_waylandCapture->stop();
        stopAudioCapture();
        m_captureActive = false;
        m_state = State::Idle;
        emit stateChanged();
        setStatusMessage(QStringLiteral("Screen capture cancelled"));
        return;
    }

    if (!m_videoFrameInput)
        m_waylandCapture->stop();
    stopAudioCapture();
    m_captureActive = false;
    m_state = State::Idle;
    emit stateChanged();
    setStatusMessage(QStringLiteral("Replay buffer stopped"));
}

void ScreenRecorder::startRecording()
{
    if (m_state == State::Recording || m_state == State::Stopping || m_state == State::AwaitingScreenPermission)
        return;

    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    if (m_preferences.captureAudio()) {
        format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
        if (!format.isSupported(QMediaFormat::Encode)) {
            emit operationFailed(QStringLiteral("Audio capture is enabled, but MP4/H.264/AAC encoding is unavailable."));
            return;
        }
        if (!m_audioSource && m_captureActive)
            startAudioCapture();
        if (m_audioCaptureStatus.startsWith(QStringLiteral("No audio input"))
            || m_audioCaptureStatus.startsWith(QStringLiteral("Could not start"))
            || m_audioCaptureStatus.startsWith(QStringLiteral("Selected audio input"))) {
            emit operationFailed(m_audioCaptureStatus);
            return;
        }
        if (m_captureActive && !m_audioSource) {
            emit operationFailed(QStringLiteral("Audio capture is enabled, but the selected input did not start."));
            return;
        }
        if (m_audioSource)
            m_sessionAudioEnabled = true;
    }
    if (!format.isSupported(QMediaFormat::Encode)) {
        emit operationFailed(QStringLiteral("This system has no Qt Multimedia encoder for MP4/H.264."));
        return;
    }
    if (!QDir().mkpath(m_outputDirectory)) {
        emit operationFailed(QStringLiteral("Could not create the video output directory."));
        return;
    }

    m_outputPath = makeOutputPath();
    m_stopRequested = false;
    m_stopCaptureRequested = false;
    m_replayBuffer.setFrameRate(m_preferences.frameRate());
    m_mediaRecorder.setMediaFormat(format);
    m_mediaRecorder.setVideoFrameRate(m_preferences.frameRate());
    m_mediaRecorder.setQuality(static_cast<QMediaRecorder::Quality>(m_preferences.quality()));
    m_mediaRecorder.setOutputLocation(QUrl::fromLocalFile(m_outputPath));

    if (m_state == State::Buffering || m_captureActive) {
        m_recordAfterCapture = false;
        m_mediaRecorder.record();
        return;
    }

    m_recordAfterCapture = true;
    m_state = State::AwaitingScreenPermission;
    emit stateChanged();
    if (m_videoFrameInput) {
        m_captureActive = true;
        if (m_preferences.captureAudio())
            startAudioCapture();
        if (m_preferences.captureAudio() && !m_audioSource)
            return;
        m_sessionAudioEnabled = bool(m_audioSource);
        m_mediaRecorder.record();
        return;
    }
    setStatusMessage(QStringLiteral("Requesting screen access from the desktop…"));
    m_waylandCapture->start();
}

void ScreenRecorder::stopRecording()
{
    if (m_state != State::Recording)
        return;

    m_state = State::Stopping;
    m_stopRequested = true;
    emit stateChanged();
    setStatusMessage(QStringLiteral("Finalizing recording…"));
    pumpSessionAudio();
    stopRecorderWhenAudioDrained();
}

void ScreenRecorder::saveReplayClip(int durationSeconds)
{
    if (m_preferences.captureAudio() && !m_replayBuffer.audioBuffered()) {
        const QString message = QStringLiteral("Audio is enabled, but no audio samples reached the replay buffer yet.");
        setStatusMessage(message);
        emit operationFailed(message);
        return;
    }
    const QString outputPath = makeReplayPath();
    if (!m_replayBuffer.saveLastClip(outputPath, durationSeconds,
                                     m_preferences.frameRate(), m_preferences.quality()))
        return;
    emit stateChanged();
}

void ScreenRecorder::saveScreenshot()
{
    if (m_latestFrame.isNull()) {
        const QString message = QStringLiteral("A screenshot is available after screen capture delivers a frame.");
        setStatusMessage(message);
        emit operationFailed(message);
        return;
    }
    if (!QDir().mkpath(m_screenshotDirectory)) {
        const QString message = QStringLiteral("Could not create the screenshot folder.");
        setStatusMessage(message);
        emit operationFailed(message);
        return;
    }

    const QString path = makeScreenshotPath();
    if (!m_latestFrame.save(path, "PNG")) {
        const QString message = QStringLiteral("Could not write the screenshot image.");
        setStatusMessage(message);
        emit operationFailed(message);
        return;
    }
    m_library.importFile(path);
    setStatusMessage(QStringLiteral("Screenshot saved and added to your library"));
    emit screenshotSaved(path);
}

QString ScreenRecorder::makeOutputPath() const
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return QDir(m_outputDirectory).filePath(QStringLiteral("Session-%1-%2.mp4").arg(timestamp, suffix));
}

QString ScreenRecorder::makeReplayPath() const
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return QDir(m_outputDirectory).filePath(QStringLiteral("Replay-%1-%2.mp4").arg(timestamp, suffix));
}

QString ScreenRecorder::makeScreenshotPath() const
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return QDir(m_screenshotDirectory).filePath(QStringLiteral("Screenshot-%1-%2.png").arg(timestamp, suffix));
}

void ScreenRecorder::updateLatestFrame(const QImage &image)
{
    if (image.isNull())
        return;
    const bool wasAvailable = !m_latestFrame.isNull();
    m_latestFrame = image.copy();
    if (!wasAvailable)
        emit stateChanged();
}

void ScreenRecorder::finalizeRecording()
{
    if (!m_stopRequested || m_state == State::Idle)
        return;
    m_stopRequested = false;

    const QFileInfo output(m_outputPath);
    if (!output.exists() || output.size() == 0) {
        QFile::remove(m_outputPath);
        fail(QStringLiteral("The recorder stopped without producing a video file."));
        return;
    }

    m_library.importFile(m_outputPath);
    emit recordingSaved(m_outputPath);
    if (m_stopCaptureRequested) {
        m_stopCaptureRequested = false;
        if (!m_videoFrameInput)
            m_waylandCapture->stop();
        m_captureActive = false;
        stopAudioCapture();
        m_state = State::Idle;
        setStatusMessage(QStringLiteral("Recording saved; screen capture stopped"));
    } else {
        m_sessionAudioEnabled = false;
        m_sessionAudioQueue.clear();
        m_sessionAudioQueuedBytes = 0;
        if (m_preferences.captureAudio())
            m_audioPollTimer.start();
        m_state = State::Buffering;
        setStatusMessage(QStringLiteral("Recording saved; replay buffer is still capturing"));
    }
    emit stateChanged();
}

void ScreenRecorder::fail(const QString &message)
{
    if (!m_videoFrameInput)
        m_waylandCapture->stop();
    m_captureActive = false;
    stopAudioCapture();
    if (m_mediaRecorder.recorderState() != QMediaRecorder::StoppedState)
        m_mediaRecorder.stop();
    m_stopRequested = false;
    m_stopCaptureRequested = false;
    m_recordAfterCapture = false;
    m_state = State::Idle;
    emit stateChanged();
    setStatusMessage(message);
    emit operationFailed(message);
}

void ScreenRecorder::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}
