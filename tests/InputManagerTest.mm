#include "AppCore.h"
#include "input/InputManager.h"

#include <QKeyEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class InputManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void rightShiftTapRequestsBack();
    void rightShiftChordDoesNotRequestBack();
    void exposesCanonicalRemappableActions();
    void capturesPersistsAndRoutesCustomKey();
    void duplicateBindingMovesToNewAction();
    void cancelAndAutoRepeatDoNotChangeBindings();
    void rejectsBuiltInAndModifierKeys();
    void ignoresInvalidStoredIdentifiers();
    void resetRestoresDefaults();
};

namespace {

void sendKey(QObject *target, QEvent::Type type, int key,
             Qt::KeyboardModifiers modifiers, quint32 nativeVirtualKey,
             bool autoRepeat = false)
{
    QKeyEvent event(type, key, modifiers, 0, nativeVirtualKey, 0,
                    QString(), autoRepeat);
    QCoreApplication::sendEvent(target, &event);
}

struct TestInputContext {
    QTemporaryDir root;
    QTemporaryDir data;
    AppCore core;
    InputManager manager;

    TestInputContext()
        : core(root.path(), data.path()), manager(&core)
    {
    }
};

}

void InputManagerTest::rightShiftTapRequestsBack()
{
    InputManager manager;
    QSignalSpy backSpy(&manager, &InputManager::mpvKeyRequested);
    QObject target;

    sendKey(&target, QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, 60);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier, 60);

    QTRY_COMPARE(backSpy.count(), 1);
    QCOMPARE(backSpy.first().first().toString(), QStringLiteral("ESC"));
}

void InputManagerTest::rightShiftChordDoesNotRequestBack()
{
    InputManager manager;
    QSignalSpy backSpy(&manager, &InputManager::mpvKeyRequested);
    QObject target;

    sendKey(&target, QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, 60);
    sendKey(&target, QEvent::KeyPress, Qt::Key_Up, Qt::ShiftModifier, 126);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_Up, Qt::ShiftModifier, 126);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier, 60);

    QTest::qWait(20);
    QCOMPARE(backSpy.count(), 0);
}

void InputManagerTest::exposesCanonicalRemappableActions()
{
    InputManager manager;
    const QVariantList actions = manager.remappableActions();

    QCOMPARE(actions.size(), 6);
    QCOMPARE(actions.first().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("up"));
    QCOMPARE(actions.last().toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("back"));
    for (const QVariant &action : actions) {
        const QVariantMap row = action.toMap();
        QVERIFY(!row.value(QStringLiteral("label")).toString().isEmpty());
        QCOMPARE(row.value(QStringLiteral("customKey")).toInt(), 0);
        QCOMPARE(row.value(QStringLiteral("value")).toString(), QStringLiteral("default"));
    }
}

void InputManagerTest::capturesPersistsAndRoutesCustomKey()
{
    TestInputContext context;
    QObject target;
    QSignalSpy captureSpy(&context.manager, &InputManager::remapCaptured);
    QSignalSpy mpvSpy(&context.manager, &InputManager::mpvKeyRequested);

    context.manager.startRemapCapture(QStringLiteral("up"));
    QVERIFY(context.manager.remapCaptureActive());
    sendKey(&target, QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, 40);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_K, Qt::NoModifier, 40);

    QVERIFY(!context.manager.remapCaptureActive());
    QCOMPARE(captureSpy.count(), 1);
    QCOMPARE(context.core.get_setting(QString(), QStringLiteral("remote_keymap.up")).toInt(),
             int(Qt::Key_K));

    sendKey(&target, QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, 40);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_K, Qt::NoModifier, 40);
    QTRY_COMPARE(mpvSpy.count(), 1);
    QCOMPARE(mpvSpy.first().first().toString(), QStringLiteral("UP"));
}

void InputManagerTest::duplicateBindingMovesToNewAction()
{
    TestInputContext context;
    QObject target;

    context.manager.startRemapCapture(QStringLiteral("up"));
    sendKey(&target, QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, 40);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_K, Qt::NoModifier, 40);
    context.manager.startRemapCapture(QStringLiteral("down"));
    sendKey(&target, QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, 40);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_K, Qt::NoModifier, 40);

    QCOMPARE(context.core.get_setting(QString(), QStringLiteral("remote_keymap.up")).toInt(), 0);
    QCOMPARE(context.core.get_setting(QString(), QStringLiteral("remote_keymap.down")).toInt(),
             int(Qt::Key_K));
}

void InputManagerTest::cancelAndAutoRepeatDoNotChangeBindings()
{
    TestInputContext context;
    QObject target;

    context.manager.startRemapCapture(QStringLiteral("left"));
    sendKey(&target, QEvent::KeyPress, Qt::Key_J, Qt::NoModifier, 38, true);
    QVERIFY(context.manager.remapCaptureActive());
    sendKey(&target, QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, 53);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier, 53);

    QVERIFY(!context.manager.remapCaptureActive());
    QCOMPARE(context.core.get_setting(QString(), QStringLiteral("remote_keymap.left")).toInt(), 0);
}

void InputManagerTest::rejectsBuiltInAndModifierKeys()
{
    TestInputContext context;
    QObject target;
    QSignalSpy rejectedSpy(&context.manager, &InputManager::remapCaptureRejected);

    context.manager.startRemapCapture(QStringLiteral("right"));
    sendKey(&target, QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier, 126);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_Up, Qt::NoModifier, 126);
    QVERIFY(context.manager.remapCaptureActive());
    QCOMPARE(rejectedSpy.count(), 1);

    sendKey(&target, QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, 59);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier, 59);
    QVERIFY(context.manager.remapCaptureActive());
    QCOMPARE(rejectedSpy.count(), 2);
    QCOMPARE(context.core.get_setting(QString(), QStringLiteral("remote_keymap.right")).toInt(), 0);
    context.manager.cancelRemapCapture();
}

void InputManagerTest::ignoresInvalidStoredIdentifiers()
{
    TestInputContext context;

    context.core.save_setting(QString(), QStringLiteral("remote_keymap.up"), -1);
    context.core.save_setting(QString(), QStringLiteral("remote_keymap.down"),
                              std::numeric_limits<int>::max());

    const QVariantList actions = context.manager.remappableActions();
    QCOMPARE(actions.at(0).toMap().value(QStringLiteral("customKey")).toInt(), 0);
    QCOMPARE(actions.at(1).toMap().value(QStringLiteral("customKey")).toInt(), 0);
}

void InputManagerTest::resetRestoresDefaults()
{
    TestInputContext context;
    QObject target;

    context.manager.startRemapCapture(QStringLiteral("select"));
    sendKey(&target, QEvent::KeyPress, Qt::Key_O, Qt::NoModifier, 31);
    sendKey(&target, QEvent::KeyRelease, Qt::Key_O, Qt::NoModifier, 31);
    QCOMPARE(context.core.get_setting(QString(), QStringLiteral("remote_keymap.select")).toInt(),
             int(Qt::Key_O));

    context.manager.resetRemappings();
    QCOMPARE(context.core.get_setting(QString(), QStringLiteral("remote_keymap.select")).toInt(), 0);
    for (const QVariant &action : context.manager.remappableActions())
        QCOMPARE(action.toMap().value(QStringLiteral("customKey")).toInt(), 0);
}

QTEST_MAIN(InputManagerTest)
#include "InputManagerTest.moc"
