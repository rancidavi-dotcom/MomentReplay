#include "global_shortcuts.h"

#include <QCoreApplication>
#include <QDir>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <windows.h>

namespace {
// RegisterHotKey reserves 0xC000–0xFFFF for shared DLLs that use global atoms.
// Moment is an application, so keep its two thread-hotkey IDs in 0x0000–0xBFFF.
constexpr int kReplayHotkeyId = 0x4D01;
constexpr int kScreenshotHotkeyId = 0x4D02;

struct Hotkey {
    UINT modifiers = MOD_NOREPEAT;
    UINT key = 0;
    QString text;
};

Hotkey parseHotkey(const QString &text)
{
    const QKeySequence sequence(text.trimmed(), QKeySequence::PortableText);
    if (sequence.count() != 1)
        return {};
    const QKeyCombination combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers();
    if (modifiers & ~(Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return {};
    Hotkey hotkey;
    if (modifiers.testFlag(Qt::ControlModifier)) hotkey.modifiers |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::ShiftModifier)) hotkey.modifiers |= MOD_SHIFT;
    if (modifiers.testFlag(Qt::AltModifier)) hotkey.modifiers |= MOD_ALT;
    if (modifiers.testFlag(Qt::MetaModifier)) hotkey.modifiers |= MOD_WIN;
    const int key = combination.key();
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        hotkey.key = UINT(key);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24 && key != Qt::Key_F12)
        hotkey.key = VK_F1 + UINT(key - Qt::Key_F1);
    else
        return {}; // F12 is reserved by Windows for debuggers.
    hotkey.text = sequence.toString(QKeySequence::PortableText);
    return hotkey;
}
}

GlobalShortcuts::GlobalShortcuts(QObject *parent)
    : GlobalShortcuts(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                          .filePath(QStringLiteral("settings.ini")), parent)
{
}

GlobalShortcuts::GlobalShortcuts(const QString &settingsFile, QObject *parent)
    : QObject(parent), m_settingsFile(settingsFile)
{
    m_activationClock.start();
    m_replayId = kReplayHotkeyId;
    m_screenshotId = kScreenshotHotkeyId;
    m_portalAvailable = true;
    QCoreApplication::instance()->installNativeEventFilter(this);
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    const QString replay = settings.value(QStringLiteral("shortcuts/windowsReplay"), "F8").toString();
    const QString screenshot = settings.value(QStringLiteral("shortcuts/windowsScreenshot"), "F9").toString();
    if (parseHotkey(replay).key) m_triggerDescription = parseHotkey(replay).text;
    if (parseHotkey(screenshot).key) m_screenshotTriggerDescription = parseHotkey(screenshot).text;
    QTimer::singleShot(0, this, &GlobalShortcuts::requestCaptureShortcuts);
}

GlobalShortcuts::~GlobalShortcuts()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
    releaseHotkeys();
}

bool GlobalShortcuts::portalAvailable() const { return m_portalAvailable; }
bool GlobalShortcuts::registered() const { return m_registered; }
bool GlobalShortcuts::screenshotRegistered() const { return m_screenshotRegistered; }
bool GlobalShortcuts::allRegistered() const { return m_registered && m_screenshotRegistered; }
bool GlobalShortcuts::requesting() const { return m_requesting; }
QString GlobalShortcuts::triggerDescription() const { return m_triggerDescription; }
QString GlobalShortcuts::screenshotTriggerDescription() const { return m_screenshotTriggerDescription; }
QString GlobalShortcuts::statusMessage() const { return m_statusMessage; }

void GlobalShortcuts::setStatus(const QString &message)
{
    m_statusMessage = message;
    emit stateChanged();
}

void GlobalShortcuts::releaseHotkeys()
{
    if (m_registered) UnregisterHotKey(nullptr, m_replayId);
    if (m_screenshotRegistered) UnregisterHotKey(nullptr, m_screenshotId);
    m_registered = m_screenshotRegistered = false;
}

bool GlobalShortcuts::registerBindings(const QString &replay, const QString &screenshot)
{
    const Hotkey first = parseHotkey(replay);
    const Hotkey second = parseHotkey(screenshot);
    if (!m_portalAvailable || !first.key || !second.key)
        return false;
    m_registered = RegisterHotKey(nullptr, m_replayId, first.modifiers, first.key);
    m_screenshotRegistered = RegisterHotKey(nullptr, m_screenshotId, second.modifiers, second.key);
    return allRegistered();
}

void GlobalShortcuts::requestCaptureShortcuts()
{
    releaseHotkeys();
    registerBindings(m_triggerDescription, m_screenshotTriggerDescription);
    setStatus(allRegistered()
        ? QStringLiteral("Global shortcuts active · replay %1 · screenshot %2")
            .arg(m_triggerDescription, m_screenshotTriggerDescription)
        : QStringLiteral("A shortcut could not be registered. Another app may be using it; choose different keys."));
}

bool GlobalShortcuts::setCaptureShortcuts(const QString &replay, const QString &screenshot)
{
    const Hotkey first = parseHotkey(replay);
    const Hotkey second = parseHotkey(screenshot);
    if (!first.key || !second.key || first.text == second.text) {
        setStatus(QStringLiteral("Use two different single shortcuts: F1–F24 (except F12), or a letter/number, with optional Ctrl, Alt, Shift or Meta."));
        return false;
    }
    releaseHotkeys();
    if (!registerBindings(first.text, second.text)) {
        releaseHotkeys();
        registerBindings(m_triggerDescription, m_screenshotTriggerDescription);
        setStatus(QStringLiteral("Those keys are unavailable. Previous shortcuts were kept."));
        return false;
    }
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("shortcuts/windowsReplay"), first.text);
    settings.setValue(QStringLiteral("shortcuts/windowsScreenshot"), second.text);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        releaseHotkeys();
        registerBindings(m_triggerDescription, m_screenshotTriggerDescription);
        setStatus(QStringLiteral("Could not save the shortcuts. Previous shortcuts were kept."));
        return false;
    }
    m_triggerDescription = first.text;
    m_screenshotTriggerDescription = second.text;
    setStatus(QStringLiteral("Global shortcuts saved · replay %1 · screenshot %2").arg(first.text, second.text));
    return true;
}

void GlobalShortcuts::reportReplayUnavailable()
{
    setStatus(QStringLiteral("Replay is not ready. Let capture run for a few seconds and try again."));
}

bool GlobalShortcuts::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const auto *native = static_cast<const MSG *>(message);
    if (native->message != WM_HOTKEY)
        return false;
    if (m_registered && native->wParam == WPARAM(m_replayId)
        && acceptActivation(QStringLiteral("save-replay")))
        emit saveReplayRequested();
    else if (m_screenshotRegistered && native->wParam == WPARAM(m_screenshotId)
             && acceptActivation(QStringLiteral("save-screenshot")))
        emit saveScreenshotRequested();
    else
        return false;
    if (result) *result = 0;
    return true;
}
