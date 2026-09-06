#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QJsonObject;
class QNetworkReply;
class NatureBackendTest;
class NatureSoundtrack;

class NatureBackend final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *soundtrack READ soundtrack CONSTANT)

public:
    explicit NatureBackend(const QString &dataRoot, QObject *parent = nullptr);
    NatureBackend(const QString &dataRoot, const QUrl &apiEndpoint, QObject *parent = nullptr);

    Q_INVOKABLE void loadLatestObservations();
    Q_INVOKABLE void refreshObservations();
    QObject *soundtrack() const;
    void setSoundtrack(NatureSoundtrack *soundtrack);
    Q_INVOKABLE void get_audio_volume_options();

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

signals:
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void refreshStarted(bool hasCachedObservations);
    void observationsLoaded(const QVariantList &observations,
                            bool fromCache,
                            bool stale);
    void loadFailed(const QString &message, bool hasCachedObservations);

private:
    friend class NatureBackendTest;
    NatureSoundtrack *m_soundtrack = nullptr;

    void beginRefresh(bool hasCachedObservations);
    void handleReply(QNetworkReply *reply);
    void beginPlaceResolution(const QVariantList &observations);
    void handlePlacesReply(QNetworkReply *reply);
    void finishRefresh(QVariantList observations);
    QNetworkReply *getJson(const QUrl &url);
    QUrl requestUrl() const;
    QUrl placesRequestUrl(const QList<qint64> &placeIds) const;
    QVariantList observationsFromPayload(const QByteArray &payload, QString *error) const;
    QVariantList observationsWithEnglishPlaces(const QVariantList &observations,
                                                const QByteArray &payload) const;
    QVariantMap itemFromObservation(const QJsonObject &observation) const;
    QUrl largePhotoUrl(const QString &url, const QString &licenseCode) const;
    QVariantMap validatedCachedItem(const QJsonObject &object) const;
    QVariantMap locationFromPlaceGuess(const QString &placeGuess) const;
    bool readCache(QVariantList *observations, QDateTime *fetchedAt) const;
    bool writeCache(const QVariantList &observations, const QDateTime &fetchedAt) const;
    bool cacheIsFresh(const QDateTime &fetchedAt) const;

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_activeReply;
    QUrl m_apiEndpoint;
    QString m_dataRoot;
    QString m_cachePath;
    QVariantList m_lastObservations;
    QVariantList m_pendingObservations;
    QElapsedTimer m_lastRequestStarted;
    bool m_refreshQueued = false;
    bool m_queuedRefreshHasCache = false;
};
