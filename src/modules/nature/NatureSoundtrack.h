#pragma once

#include <QObject>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QVariantList>

class AppCore;
class AudioCrossfadePlayer;
class NatureSoundtrackTest;
class QNetworkReply;

class NatureSoundtrack final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool requested READ requested NOTIFY requestedChanged)
    Q_PROPERTY(QString sourceUrl READ sourceUrl NOTIFY currentSoundChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentSoundChanged)
public:
    NatureSoundtrack(const QString &appRoot, const QString &dataRoot,
                     AppCore *appCore, QObject *parent = nullptr);
    bool active() const;
    bool requested() const { return m_wanted; }
    void routeKey(const QString &key) { emit inputKey(key); }
    QString sourceUrl() const;
    QString currentTitle() const;
    Q_INVOKABLE void prepare();
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setPaused(bool paused);
    void settingChanged(const QString &key, const QVariant &value);
    static QVariantList parseCatalog(const QByteArray &payload);
    static QUrl catalogEndpoint();

signals:
    void statusChanged(const QString &message);
    void activeChanged();
    void requestedChanged();
    void currentSoundChanged();
    void inputKey(const QString &key);

private:
    friend class NatureSoundtrackTest;
    void tryStart();
    void refresh();
    bool readCache();
    void writeCache();
    void cancelRequest();
    bool enabled() const;

    QString m_cachePath;
    AppCore *m_appCore;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QUrl m_endpoint;
    QByteArray m_payload;
    QVariantList m_tracks;
    QVariantMap m_current;
    QDateTime m_fetchedAt;
    QDateTime m_retryAfter;
    AudioCrossfadePlayer *m_player;
    bool m_wanted = false;
    bool m_paused = false;
    bool m_failed = false;
};
