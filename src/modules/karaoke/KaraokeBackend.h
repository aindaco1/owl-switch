#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QVariantList>
#include <QVector>

#include "tools/YouTubeJob.h"

class KaraokeBackend : public QObject {
    Q_OBJECT
public:
    explicit KaraokeBackend(const QString &appRoot, const QString &dataRoot,
                            QObject *parent = nullptr);
    ~KaraokeBackend() override;

    Q_INVOKABLE void loadCatalog();
    Q_INVOKABLE void loadCatalogDeferred();
    Q_INVOKABLE void refreshCatalog();
    Q_INVOKABLE QVariantMap searchCatalog(const QString &query,
                                          int offset = 0,
                                          int limit = 250) const;
    Q_INVOKABLE QVariantList getQueue() const;
    Q_INVOKABLE bool enqueue(const QVariantMap &song);
    Q_INVOKABLE bool removeQueueEntry(const QString &entryId);
    Q_INVOKABLE bool moveQueueEntry(int fromIndex, int toIndex);
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void clearQueueExcept(const QString &entryId);
    Q_INVOKABLE bool completeQueueEntry(const QString &entryId);
    Q_INVOKABLE bool failQueueEntry(const QString &entryId, const QString &message);
    Q_INVOKABLE bool resetQueueEntry(const QString &entryId);
    Q_INVOKABLE void prefetchQueueEntry(const QString &entryId);
    Q_INVOKABLE QString cachedPlaybackPath(const QString &videoId) const;
    Q_INVOKABLE QString writePlaybackPlaylist();

    static QString cleanedTitle(const QString &rawTitle,
                                const QString &sourceId = {});
    static QString deduplicationKey(const QString &title);
    static QVariantList deduplicatedCatalog(const QVariantList &items);
    static bool isValidVideoId(const QString &videoId);
    static bool isPlausibleCatalogSize(int candidateItemCount, int cachedItemCount,
                                       int minimumItemCount = 100);
    static QVariantMap catalogSongFromJson(const QJsonObject &object);

signals:
    void catalogLoadStarted(bool hasCachedCatalog);
    void catalogReset(const QVariant &items, bool fromCache);
    void catalogItemsAppended(const QVariant &items);
    void catalogLoadFinished(int itemCount, bool fromCache);
    void catalogLoadFailed(const QString &message, bool usingCache);
    void catalogChanged(int itemCount, bool fromCache);
    void queueChanged(const QVariant &items);
    void queueEntryPrefetchStarted(const QString &entryId);
    void queueEntryPrefetched(const QString &entryId, const QString &filePath);
    void queueEntryPrefetchFailed(const QString &entryId, const QString &message);

private:
    static constexpr int kCatalogSchemaVersion = 12;
    static constexpr int kQueueSchemaVersion = 1;
    static constexpr int kCatalogRefreshHours = 24;

    QString catalogFilePath() const;
    QString queueFilePath() const;
    QString playlistFilePath() const;
    QString playbackCacheDirectory() const;
    QString canonicalVideoUrl(const QString &videoId) const;
    QString findCachedPlaybackPath(const QString &videoId) const;

    bool loadCatalogCache();
    bool saveCatalogCache() const;
    void startCatalogRefresh();
    void consumeCatalogOutput(bool includeRemainder = false);
    void consumeCatalogLine(const QByteArray &line);
    void flushCatalogBatch();
    void rebuildCatalogIndex(const QVariantList &items, bool refreshItems);
    void finishCatalogRefresh(YouTubeJob::Failure failure, int exitCode,
                              const QString &safeError);
    bool catalogCacheIsStale() const;
    bool catalogSourcesArePlausible() const;

    void loadQueue();
    bool saveQueue() const;
    int queueIndexForEntryId(const QString &entryId) const;
    QVariantMap validatedQueueEntry(const QVariantMap &candidate,
                                    bool createEntryId) const;
    bool publishQueue();
    void stopPrefetchProcess(bool removeArtifacts);
    void cleanupPrefetchArtifacts(const QString &videoId) const;
    void finishPrefetch(YouTubeJob::Failure failure, int exitCode,
                        const QString &safeError);
    void prunePlaybackCache();

    QString m_appRoot;
    QString m_dataRoot;
    QVariantList m_catalog;
    QVariantList m_queue;
    QHash<QString, int> m_catalogSourceCounts;
    QDateTime m_catalogFetchedAt;
    bool m_catalogLoaded = false;
    bool m_catalogLoadScheduled = false;
    bool m_autoRefreshChecked = false;

    YouTubeJob *m_catalogJob = nullptr;
    QByteArray m_catalogOutputBuffer;
    QVariantList m_refreshItems;
    QVariantList m_refreshBatch;
    QSet<QString> m_refreshIds;
    QHash<QString, int> m_refreshPreferredPriorityByKey;
    QHash<QString, int> m_refreshSourceCounts;
    bool m_refreshHadCache = false;

    struct CatalogSearchEntry {
        int itemIndex = -1;
        QString searchKey;
        QString sortKey;
        QString titleKey;
        QString videoId;
    };
    QVector<CatalogSearchEntry> m_catalogSearchIndex;
    bool m_catalogIndexUsesRefreshItems = false;

    YouTubeJob *m_prefetchJob = nullptr;
    QString m_prefetchEntryId;
    QString m_prefetchVideoId;
};
