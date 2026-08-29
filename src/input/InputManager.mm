#include "InputManager.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QQuickWindow>

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

static void installHandlers(GCController *controller, InputManager *manager);

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

InputManager::InputManager(QObject *parent) : QObject(parent) {
    QCoreApplication::instance()->installEventFilter(this);
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

void InputManager::setGamepadCount(int count) {
    count = qMax(0, count);
    if (m_gamepadCount == count) return;
    m_gamepadCount = count;
    emit gamepadConnectedChanged();
    emit hintsChanged();
}

void InputManager::deliverKey(int qtKey, const QString &mpvKey) {
    if (!m_window || QGuiApplication::applicationState() != Qt::ApplicationActive ||
        !m_window->isActive()) {
        emit mpvKeyRequested(mpvKey);
        return;
    }
    QCoreApplication::postEvent(m_window, new QKeyEvent(QEvent::KeyPress, qtKey, Qt::NoModifier));
    QCoreApplication::postEvent(m_window, new QKeyEvent(QEvent::KeyRelease, qtKey, Qt::NoModifier));
}

bool InputManager::eventFilter(QObject *object, QEvent *event) {
    Q_UNUSED(object)
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
    return false;
}

static void bindButton(GCControllerButtonInput *button, InputManager *manager,
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
    bindButton(pad.dpad.up, manager, Qt::Key_Up, QStringLiteral("UP"));
    bindButton(pad.dpad.down, manager, Qt::Key_Down, QStringLiteral("DOWN"));
    bindButton(pad.dpad.left, manager, Qt::Key_Left, QStringLiteral("LEFT"));
    bindButton(pad.dpad.right, manager, Qt::Key_Right, QStringLiteral("RIGHT"));
    bindButton(pad.buttonA, manager, Qt::Key_Return, QStringLiteral("ENTER"));
    bindButton(pad.buttonB, manager, Qt::Key_Escape, QStringLiteral("ESC"));
    bindButton(pad.buttonX, manager, Qt::Key_Space, QStringLiteral("SPACE"));
    bindButton(pad.buttonY, manager, Qt::Key_M, QStringLiteral("m"));
    bindButton(pad.leftShoulder, manager, Qt::Key_Left, QStringLiteral("LEFT"));
    bindButton(pad.rightShoulder, manager, Qt::Key_Right, QStringLiteral("RIGHT"));
    if (@available(macOS 10.15, *))
        bindButton(pad.buttonMenu, manager, Qt::Key_Escape, QStringLiteral("ESC"));
}
