#include "input/RightShiftBackTracker.h"

#include <QtTest>

class RightShiftBackTrackerTest final : public QObject {
    Q_OBJECT

private slots:
    void tapRequestsBack();
    void chordRemainsAModifier();
    void autoRepeatDoesNotResetChordState();
};

void RightShiftBackTrackerTest::tapRequestsBack()
{
    RightShiftBackTracker tracker;
    tracker.press(false);
    QVERIFY(tracker.pressed());
    QVERIFY(tracker.release(false));
    QVERIFY(!tracker.pressed());
}

void RightShiftBackTrackerTest::chordRemainsAModifier()
{
    RightShiftBackTracker tracker;
    tracker.press(false);
    tracker.useAsModifier();
    QVERIFY(!tracker.release(false));
    QVERIFY(!tracker.pressed());
}

void RightShiftBackTrackerTest::autoRepeatDoesNotResetChordState()
{
    RightShiftBackTracker tracker;
    tracker.press(false);
    tracker.useAsModifier();
    tracker.press(true);
    QVERIFY(!tracker.release(true));
    QVERIFY(tracker.pressed());
    QVERIFY(!tracker.release(false));
}

QTEST_GUILESS_MAIN(RightShiftBackTrackerTest)
#include "RightShiftBackTrackerTest.moc"
