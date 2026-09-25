#pragma once
#include <QObject>
#include <QString>
#include <DustWave/SparkleUpdater.h>

class UpdateManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
public:
    explicit UpdateManager(const QString &dataRoot, QObject *parent = nullptr);
    UpdateManager(DustWave::UpdateDriver *driver, QObject *parent);
    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    QString state() const { return m_state; }
    QString statusMessage() const { return m_statusMessage; }
    bool updateAvailable() const { return !m_latestVersion.isEmpty(); }
    Q_INVOKABLE void checkForUpdates();
    void checkForUpdatesOnLaunch();
signals:
    void changed();
    void launchUpdateAvailable(const QString &version);
private:
    void startCheck(bool launch);
    void setStatus(const QString &state, const QString &message);
    DustWave::UpdateDriver *m_driver;
    QString m_latestVersion;
    QString m_state = QStringLiteral("idle");
    QString m_statusMessage = QStringLiteral("Check for a newer signed release.");
    bool m_checking = false;
    bool m_launch = false;
    bool m_launchChecked = false;
};
