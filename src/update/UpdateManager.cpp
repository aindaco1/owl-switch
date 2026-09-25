#include "UpdateManager.h"
#include <QCoreApplication>

UpdateManager::UpdateManager(const QString &dataRoot, QObject *parent)
    : UpdateManager(new DustWave::SparkleUpdater, parent) {
    Q_UNUSED(dataRoot);
    m_driver->setParent(this);
}
UpdateManager::UpdateManager(DustWave::UpdateDriver *driver, QObject *parent)
    : QObject(parent), m_driver(driver) {
    Q_ASSERT(driver);
    connect(driver, &DustWave::UpdateDriver::available, this, [this](const QString &version) {
        m_latestVersion = version;
        setStatus(QStringLiteral("available"), QStringLiteral("Version %1 is available. Review the update to download and install.").arg(version));
        if (m_launch) emit launchUpdateAvailable(version);
    });
    connect(driver, &DustWave::UpdateDriver::current, this, [this] {
        m_latestVersion.clear();
        setStatus(QStringLiteral("upToDate"), QStringLiteral("Version %1 is up to date.").arg(currentVersion()));
    });
    connect(driver, &DustWave::UpdateDriver::failed, this, [this] {
        setStatus(QStringLiteral("error"), QStringLiteral("Could not check for updates. Try again later."));
    });
    connect(driver, &DustWave::UpdateDriver::finished, this, [this] {
        m_checking = false; m_launch = false;
        if (m_state == QStringLiteral("checking")) setStatus(QStringLiteral("idle"), QStringLiteral("Check for a newer signed release."));
        else emit changed();
    });
}
QString UpdateManager::currentVersion() const { return QCoreApplication::applicationVersion(); }
void UpdateManager::setStatus(const QString &state, const QString &message) {
    m_state = state; m_statusMessage = message; emit changed();
}
void UpdateManager::checkForUpdates() { startCheck(false); }
void UpdateManager::checkForUpdatesOnLaunch() { startCheck(true); }
void UpdateManager::startCheck(bool launch) {
    if (m_checking || !m_driver->canCheck() || (launch && m_launchChecked)) return;
    if (launch) m_launchChecked = true;
    m_launch = launch; m_checking = true;
    setStatus(QStringLiteral("checking"), QStringLiteral("Checking for updates..."));
    m_driver->check(launch);
}
