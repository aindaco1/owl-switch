#pragma once

#include <QObject>
#include <QCryptographicHash>
#include <QFile>
#include <QPointer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class UpdateManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall CONSTANT)

public:
    explicit UpdateManager(const QString &dataRoot, QObject *parent = nullptr);
    UpdateManager(const QString &dataRoot, QNetworkAccessManager *network,
                  QObject *parent);

    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    QString state() const { return m_state; }
    QString statusMessage() const { return m_statusMessage; }
    double progress() const { return m_progress; }
    bool updateAvailable() const;
    bool canInstall() const { return m_canInstall; }

    Q_INVOKABLE void checkForUpdates();
    void checkForUpdatesOnLaunch();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void installAndRestart();
    Q_INVOKABLE void openDownloadedUpdate();

    static int compareVersions(const QString &left, const QString &right);

signals:
    void changed();
    void launchUpdateAvailable(const QString &version);

private:
    void startCheck(bool startedOnLaunch);
    void resetTransfer();
    void setStatus(const QString &state, const QString &message);
    void handleReleaseResponse(QNetworkReply *reply);
    void handleDownloadFinished(QNetworkReply *reply);
    bool verifyDiskImage(const QString &path, QString *errorMessage) const;
    bool verifyAppBundle(const QString &appPath, QString *errorMessage) const;
    QString runningAppBundlePath() const;
    QString teamIdentifierForApp(const QString &appPath) const;

    QString m_dataRoot;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile m_downloadFile;
    QCryptographicHash m_downloadHash{QCryptographicHash::Sha256};
    QUrl m_assetUrl;
    QString m_assetName;
    QString m_expectedDigest;
    qint64 m_expectedSize = -1;
    QString m_downloadedPath;
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_state = QStringLiteral("idle");
    QString m_statusMessage = QStringLiteral("Check GitHub for a newer signed release.");
    double m_progress = 0.0;
    QString m_runningAppPath;
    QString m_expectedTeamIdentifier;
    bool m_canInstall = false;
    bool m_checkStartedOnLaunch = false;
};
