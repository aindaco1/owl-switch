#include <QtTest>
#include <QSignalSpy>
#include "RecoveryEvidence.h"
#include "update/UpdateManager.h"

class Driver final : public DustWave::UpdateDriver {
public:
    using UpdateDriver::UpdateDriver;
    int checks = 0;
    bool informationOnly = false;
    bool allowed = true;
    bool canCheck() const override { return allowed; }
    void check(bool information) override { checks++; informationOnly = information; }
    void found() { emit available(QStringLiteral("1.7.0")); emit finished(); }
    void noUpdate() { emit current(); emit finished(); }
    void fail() { emit failed(); emit finished(); }
};
class UpdateManagerTest : public QObject {
    Q_OBJECT
private slots:
    void launchOnlyChecksInformationOnceAndNotifiesOnAvailability() {
        Driver driver; UpdateManager manager(&driver, nullptr);
        QSignalSpy available(&manager, &UpdateManager::launchUpdateAvailable);
        manager.checkForUpdatesOnLaunch(); QVERIFY(driver.informationOnly);
        manager.checkForUpdatesOnLaunch(); QCOMPARE(driver.checks, 1);
        driver.found(); QCOMPARE(available.count(), 1); QVERIFY(manager.updateAvailable());
        manager.checkForUpdatesOnLaunch(); QCOMPARE(driver.checks, 1);
    }
    void launchStaysQuietOnCurrentAndFailure() {
        for (bool failed : {false, true}) {
            Driver driver; UpdateManager manager(&driver, nullptr);
            QSignalSpy available(&manager, &UpdateManager::launchUpdateAvailable);
            manager.checkForUpdatesOnLaunch();
            if (failed) driver.fail(); else driver.noUpdate();
            QCOMPARE(available.count(), 0);
            QCOMPARE(manager.state(), failed ? QStringLiteral("error") : QStringLiteral("upToDate"));
        }
    }
    void manualFallbackRequestsTheUserDrivenSparkleFlow() {
        Driver driver; UpdateManager manager(&driver, nullptr);
        QSignalSpy available(&manager, &UpdateManager::launchUpdateAvailable);
        manager.checkForUpdatesOnLaunch(); driver.fail();
        QCOMPARE(manager.state(), QStringLiteral("error"));
        QCOMPARE(available.count(), 0);
        const QString failure = manager.statusMessage();
        manager.checkForUpdates(); QVERIFY(!driver.informationOnly); QCOMPARE(driver.checks, 2);
        driver.noUpdate(); QCOMPARE(available.count(), 0);
        QCOMPARE(manager.state(), QStringLiteral("upToDate"));
        QVERIFY(writeRecoveryEvidence("update-response", failure,
            {{"launch_prompt_absent", true}, {"manual_retry_recovers", true}}));
    }
    void busyDriverDoesNotStartAnotherCheck() {
        Driver driver; driver.allowed = false; UpdateManager manager(&driver, nullptr);
        manager.checkForUpdates(); manager.checkForUpdatesOnLaunch(); QCOMPARE(driver.checks, 0);
        driver.allowed = true; manager.checkForUpdatesOnLaunch(); QCOMPARE(driver.checks, 1);
    }
};
QTEST_GUILESS_MAIN(UpdateManagerTest)
#include "UpdateManagerTest.moc"
