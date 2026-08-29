#include "input/InputManager.h"

#include <QKeyEvent>
#include <QSignalSpy>
#include <QtTest>

class InputManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void rightShiftTapRequestsBack();
    void rightShiftChordDoesNotRequestBack();
};

namespace {

void sendKey(QObject *target, QEvent::Type type, int key,
             Qt::KeyboardModifiers modifiers, quint32 nativeVirtualKey)
{
    QKeyEvent event(type, key, modifiers, 0, nativeVirtualKey, 0);
    QCoreApplication::sendEvent(target, &event);
}

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

QTEST_MAIN(InputManagerTest)
#include "InputManagerTest.moc"
