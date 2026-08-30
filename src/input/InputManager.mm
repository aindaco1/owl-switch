#include "InputManager.h"
#include "../AppCore.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQuickWindow>

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

static void installHandlers(GCController *controller, InputManager *manager);

namespace {
constexpr quint32 kSyntheticScanCode = 0x240F00D;
constexpr int kMouseButtonBase = 0x03000000;

bool textInputHasFocus()
{
    QObject *focusObject = QGuiApplication::focusObject();
    if (!focusObject)
        return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(focusObject, &query);
    return query.value(Qt::ImEnabled).toBool();
}

bool isModifierOnlyKey(int key)
{
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
           key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_CapsLock;
}

bool isCancelKey(int key)
{
    return key == Qt::Key_Escape || key == Qt::Key_Backspace || key == Qt::Key_Back;
}
}

void inputManagerControllerCountChanged(InputManager *manager, int count) {
    if (!manager) return;
    QMetaObject::invokeMethod(manager, [manager, count]() { manager->setGamepadCount(count); },
                              Qt::QueuedConnection);
}

void inputManagerDeliverKey(InputManager *manager, int key, const QString &mpvKey) {
    if (!manager) return;
    QMetaObject::invokeMethod(manager, [manager, key, mpvKey]() {
        manager->deliverKey(key, mpvKey);
    }, Qt::QueuedConnection);
}

void inputManagerDeliverAction(InputManager *manager, const QString &actionId) {
    if (!manager) return;
    QMetaObject::invokeMethod(manager, [manager, actionId]() {
        manager->deliverAction(manager->actionFromId(actionId));
    }, Qt::QueuedConnection);
}

InputManager::InputManager(AppCore *appCore, QObject *parent)
    : QObject(parent), m_appCore(appCore) {
    QCoreApplication::instance()->installEventFilter(this);
    loadRemappings();
    if (m_appCore) {
        connect(m_appCore, &AppCore::appSettingChanged,
                this, &InputManager::onAppSettingChanged);
    }
    QPointer<InputManager> guardedManager(this);
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    id connectObserver = [center addObserverForName:GCControllerDidConnectNotification
                                             object:nil queue:NSOperationQueue.mainQueue
                                        usingBlock:^(NSNotification *note) {
        InputManager *strongManager = guardedManager.data();
        if (!strongManager) return;
        installHandlers((GCController *)note.object, strongManager);
        inputManagerControllerCountChanged(strongManager, (int)GCController.controllers.count);
    }];
    id disconnectObserver = [center addObserverForName:GCControllerDidDisconnectNotification
                                                object:nil queue:NSOperationQueue.mainQueue
                                           usingBlock:^(NSNotification *) {
        InputManager *strongManager = guardedManager.data();
        if (strongManager)
            inputManagerControllerCountChanged(strongManager, (int)GCController.controllers.count);
    }];
    m_connectObserver = (__bridge void *)[connectObserver retain];
    m_disconnectObserver = (__bridge void *)[disconnectObserver retain];

    [GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];
    for (GCController *controller in GCController.controllers)
        installHandlers(controller, this);
    setGamepadCount((int)GCController.controllers.count);
}

InputManager::~InputManager() {
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    if (m_connectObserver) {
        id observer = (__bridge id)m_connectObserver;
        [center removeObserver:observer];
        [observer release];
    }
    if (m_disconnectObserver) {
        id observer = (__bridge id)m_disconnectObserver;
        [center removeObserver:observer];
        [observer release];
    }
    [GCController stopWirelessControllerDiscovery];
}

void InputManager::setTargetWindow(QQuickWindow *window) { m_window = window; }

QVariantMap InputManager::hints() const {
    if (gamepadConnected()) {
        return {{"back", "B"}, {"navigate", "D-PAD"}, {"select", "A"},
                {"playPause", "X"}};
    }
    return {{"back", "ESC"}, {"navigate", "ARROWS"}, {"select", "ENTER"},
            {"playPause", "SPACE"}};
}

const std::array<InputManager::ActionDescriptor, 7> &InputManager::actionDescriptors()
{
    static const std::array<ActionDescriptor, 7> descriptors{{
        {Action::Up, "up", "Up", Qt::Key_Up, "UP", true},
        {Action::Down, "down", "Down", Qt::Key_Down, "DOWN", true},
        {Action::Left, "left", "Left", Qt::Key_Left, "LEFT", true},
        {Action::Right, "right", "Right", Qt::Key_Right, "RIGHT", true},
        {Action::Select, "select", "Select / OK", Qt::Key_Return, "ENTER", true},
        {Action::Back, "back", "Back", Qt::Key_Escape, "ESC", true},
        {Action::PlayPause, "play_pause", "Play / Pause", Qt::Key_Space, "SPACE", false},
    }};
    return descriptors;
}

InputManager::Action InputManager::actionFromId(const QString &actionId)
{
    for (const auto &descriptor : actionDescriptors()) {
        if (actionId == QLatin1String(descriptor.id))
            return descriptor.action;
    }
    return Action::None;
}

int InputManager::qtKeyForAction(Action action)
{
    for (const auto &descriptor : actionDescriptors()) {
        if (descriptor.action == action)
            return descriptor.qtKey;
    }
    return 0;
}

QString InputManager::mpvKeyForAction(Action action)
{
    for (const auto &descriptor : actionDescriptors()) {
        if (descriptor.action == action)
            return QString::fromLatin1(descriptor.mpvKey);
    }
    return QString();
}

bool InputManager::isReservedDefaultKey(int key)
{
    if (key == Qt::Key_Backspace || key == Qt::Key_Back || key == Qt::Key_Enter ||
        key == Qt::Key_Space) {
        return true;
    }
    for (const auto &descriptor : actionDescriptors()) {
        if (descriptor.qtKey == key)
            return true;
    }
    return false;
}

bool InputManager::isValidInputId(int key)
{
    if (key > 0 && key < Qt::Key_unknown)
        return true;
    if (key < kMouseButtonBase)
        return false;
    const int button = key - kMouseButtonBase;
    return button == Qt::LeftButton || button == Qt::RightButton ||
           button == Qt::MiddleButton || button == Qt::BackButton ||
           button == Qt::ForwardButton;
}

int InputManager::customKeyForAction(Action action) const
{
    for (auto it = m_keyRemap.cbegin(); it != m_keyRemap.cend(); ++it) {
        if (it.value() == action)
            return it.key();
    }
    return 0;
}

QString InputManager::keyDisplayName(int extendedKeyId) const
{
    if (extendedKeyId == 0)
        return QString();
    if (extendedKeyId >= kMouseButtonBase) {
        switch (Qt::MouseButton(extendedKeyId - kMouseButtonBase)) {
        case Qt::LeftButton: return QStringLiteral("Mouse: Left Click");
        case Qt::RightButton: return QStringLiteral("Mouse: Right Click");
        case Qt::MiddleButton: return QStringLiteral("Mouse: Middle Click");
        case Qt::BackButton: return QStringLiteral("Mouse: Back");
        case Qt::ForwardButton: return QStringLiteral("Mouse: Forward");
        default: return QStringLiteral("Mouse Button %1").arg(extendedKeyId - kMouseButtonBase);
        }
    }
    return QKeySequence(extendedKeyId).toString(QKeySequence::NativeText);
}

QVariantList InputManager::remappableActions() const
{
    QVariantList actions;
    for (const auto &descriptor : actionDescriptors()) {
        if (!descriptor.remappable)
            continue;
        const int customKey = customKeyForAction(descriptor.action);
        actions.append(QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(descriptor.id)},
            {QStringLiteral("label"), QString::fromLatin1(descriptor.label)},
            {QStringLiteral("customKey"), customKey},
            {QStringLiteral("value"), customKey == 0
                ? QStringLiteral("default")
                : QStringLiteral("default + %1").arg(keyDisplayName(customKey))},
        });
    }
    return actions;
}

void InputManager::loadRemappings()
{
    m_keyRemap.clear();
    if (!m_appCore) {
        emit remappingsChanged();
        return;
    }
    for (const auto &descriptor : actionDescriptors()) {
        if (!descriptor.remappable)
            continue;
        bool ok = false;
        const int key = m_appCore->get_setting(
            QString(), QStringLiteral("remote_keymap.") + QString::fromLatin1(descriptor.id))
                            .toInt(&ok);
        if (ok && isValidInputId(key) && !isReservedDefaultKey(key) &&
            !isModifierOnlyKey(key)) {
            m_keyRemap.insert(key, descriptor.action);
        }
    }
    emit remappingsChanged();
}

void InputManager::onAppSettingChanged(const QString &key, const QString &value)
{
    Q_UNUSED(value)
    if (key.startsWith(QStringLiteral("remote_keymap.")))
        loadRemappings();
}

void InputManager::startRemapCapture(const QString &actionId)
{
    const Action action = actionFromId(actionId);
    if (action == Action::None || action == Action::PlayPause)
        return;
    m_captureAction = action;
    if (!m_remapCaptureActive) {
        m_remapCaptureActive = true;
        emit remapCaptureActiveChanged();
    }
}

void InputManager::cancelRemapCapture()
{
    m_captureAction = Action::None;
    if (m_remapCaptureActive) {
        m_remapCaptureActive = false;
        emit remapCaptureActiveChanged();
    }
}

void InputManager::completeRemapCapture(int extendedKeyId)
{
    if (!m_remapCaptureActive || m_captureAction == Action::None)
        return;
    if (!isValidInputId(extendedKeyId)) {
        emit remapCaptureRejected(QStringLiteral("That input cannot be mapped."));
        return;
    }
    if (isModifierOnlyKey(extendedKeyId)) {
        emit remapCaptureRejected(QStringLiteral("Modifier keys must be combined with another key."));
        return;
    }
    if (isReservedDefaultKey(extendedKeyId)) {
        emit remapCaptureRejected(QStringLiteral("That key is already a built-in control."));
        return;
    }

    const Action capturedAction = m_captureAction;
    QString capturedId;
    for (const auto &descriptor : actionDescriptors()) {
        if (!descriptor.remappable)
            continue;
        const QString id = QString::fromLatin1(descriptor.id);
        if (descriptor.action == capturedAction)
            capturedId = id;
        if (!m_appCore)
            continue;
        bool ok = false;
        const int existing = m_appCore->get_setting(
            QString(), QStringLiteral("remote_keymap.") + id).toInt(&ok);
        if (ok && existing == extendedKeyId && descriptor.action != capturedAction)
            m_appCore->save_setting(QString(), QStringLiteral("remote_keymap.") + id, 0);
    }

    if (m_appCore) {
        m_appCore->save_setting(QString(),
            QStringLiteral("remote_keymap.") + capturedId, extendedKeyId);
    } else {
        for (auto it = m_keyRemap.begin(); it != m_keyRemap.end();) {
            if (it.value() == capturedAction)
                it = m_keyRemap.erase(it);
            else
                ++it;
        }
        m_keyRemap.insert(extendedKeyId, capturedAction);
        emit remappingsChanged();
    }
    cancelRemapCapture();
    emit remapCaptured(capturedId, keyDisplayName(extendedKeyId));
}

void InputManager::resetRemappings()
{
    cancelRemapCapture();
    if (m_appCore) {
        for (const auto &descriptor : actionDescriptors()) {
            if (!descriptor.remappable)
                continue;
            m_appCore->save_setting(QString(),
                QStringLiteral("remote_keymap.") + QString::fromLatin1(descriptor.id), 0);
        }
    } else {
        m_keyRemap.clear();
        emit remappingsChanged();
    }
}

void InputManager::setGamepadCount(int count) {
    count = qMax(0, count);
    if (m_gamepadCount == count) return;
    m_gamepadCount = count;
    emit gamepadConnectedChanged();
    emit hintsChanged();
}

void InputManager::deliverAction(Action action) {
    if (action == Action::None)
        return;
    deliverKey(qtKeyForAction(action), mpvKeyForAction(action));
}

void InputManager::deliverKey(int qtKey, const QString &mpvKey) {
    if (!m_window || QGuiApplication::applicationState() != Qt::ApplicationActive ||
        !m_window->isActive()) {
        emit mpvKeyRequested(mpvKey);
        return;
    }
    QCoreApplication::postEvent(m_window,
        new QKeyEvent(QEvent::KeyPress, qtKey, Qt::NoModifier,
                      kSyntheticScanCode, 0, 0, QString(), false));
    QCoreApplication::postEvent(m_window,
        new QKeyEvent(QEvent::KeyRelease, qtKey, Qt::NoModifier,
                      kSyntheticScanCode, 0, 0, QString(), false));
}

bool InputManager::eventFilter(QObject *object, QEvent *event) {
    Q_UNUSED(object)
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonDblClick ||
        event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const bool pressed = event->type() != QEvent::MouseButtonRelease;
        const int extendedKeyId = kMouseButtonBase + int(mouse->button());
        if (m_remapCaptureActive) {
            if (pressed)
                completeRemapCapture(extendedKeyId);
            return true;
        }
        const Action action = m_keyRemap.value(extendedKeyId, Action::None);
        if (action != Action::None) {
            if (pressed)
                deliverKey(qtKeyForAction(action), mpvKeyForAction(action));
            return true;
        }
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        // macOS virtual key code 60 is Right Shift. A tap remains a one-handed
        // Back action, but holding it with another key must remain a real Shift
        // modifier for queue reordering and text entry.
        if (key->key() == Qt::Key_Shift && key->nativeVirtualKey() == 60) {
            m_rightShiftBackTracker.press(key->isAutoRepeat());
            return false;
        }
        if (m_rightShiftBackTracker.pressed())
            m_rightShiftBackTracker.useAsModifier();
    } else if (event->type() == QEvent::KeyRelease) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Shift && key->nativeVirtualKey() == 60 &&
            m_rightShiftBackTracker.release(key->isAutoRepeat())) {
            deliverKey(Qt::Key_Escape, QStringLiteral("ESC"));
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const bool synthetic = key->nativeScanCode() == kSyntheticScanCode;
        if (m_remapCaptureActive && !synthetic) {
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                if (isCancelKey(key->key()))
                    cancelRemapCapture();
                else
                    completeRemapCapture(key->key());
            }
            return true;
        }

        if (!synthetic) {
            const Action action = m_keyRemap.value(key->key(), Action::None);
            if (action != Action::None && !textInputHasFocus()) {
                if (event->type() == QEvent::KeyPress)
                    deliverKey(qtKeyForAction(action), mpvKeyForAction(action));
                return true;
            }
        }
    }
    return false;
}

static void bindActionButton(GCControllerButtonInput *button, InputManager *manager,
                             const QString &actionId) {
    if (!button) return;
    QPointer<InputManager> guardedManager(manager);
    button.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        InputManager *strongManager = guardedManager.data();
        if (pressed && strongManager)
            inputManagerDeliverAction(strongManager, actionId);
    };
}

static void bindKeyButton(GCControllerButtonInput *button, InputManager *manager,
                          int qtKey, const QString &mpvKey) {
    if (!button) return;
    QPointer<InputManager> guardedManager(manager);
    button.pressedChangedHandler = ^(GCControllerButtonInput *, float, BOOL pressed) {
        InputManager *strongManager = guardedManager.data();
        if (pressed && strongManager)
            inputManagerDeliverKey(strongManager, qtKey, mpvKey);
    };
}

static void installHandlers(GCController *controller, InputManager *manager) {
    GCExtendedGamepad *pad = controller.extendedGamepad;
    if (!pad) return;
    bindActionButton(pad.dpad.up, manager, QStringLiteral("up"));
    bindActionButton(pad.dpad.down, manager, QStringLiteral("down"));
    bindActionButton(pad.dpad.left, manager, QStringLiteral("left"));
    bindActionButton(pad.dpad.right, manager, QStringLiteral("right"));
    bindActionButton(pad.buttonA, manager, QStringLiteral("select"));
    bindActionButton(pad.buttonB, manager, QStringLiteral("back"));
    bindActionButton(pad.buttonX, manager, QStringLiteral("play_pause"));
    bindKeyButton(pad.buttonY, manager, Qt::Key_M, QStringLiteral("m"));
    bindActionButton(pad.leftShoulder, manager, QStringLiteral("left"));
    bindActionButton(pad.rightShoulder, manager, QStringLiteral("right"));
    if (@available(macOS 10.15, *))
        bindActionButton(pad.buttonMenu, manager, QStringLiteral("back"));
}
