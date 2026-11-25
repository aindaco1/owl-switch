#pragma once

#include <QHash>
#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariant>

class JellyfinBackend : public QObject {
    Q_OBJECT
public:
    explicit JellyfinBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    Q_INVOKABLE QString get_auth_state();
    Q_INVOKABLE QString get_active_user_name();
    Q_INVOKABLE QString get_active_server_name();
    Q_INVOKABLE QString get_last_server_url();

    Q_INVOKABLE void authenticate(const QString &serverUrl, const QString &username, const QString &password);
    Q_INVOKABLE void start_quick_connect(const QString &serverUrl);
    Q_INVOKABLE void cancel_quick_connect();
    Q_INVOKABLE void logout();

    Q_INVOKABLE void load_libraries();
    Q_INVOKABLE void load_items(const QString &libraryId);
    Q_INVOKABLE void load_items_for_type(const QString &parentId, const QString &itemType, bool recursive);
    Q_INVOKABLE void load_item_detail(const QString &itemId);
    Q_INVOKABLE void load_continue_watching();
    Q_INVOKABLE void load_up_next();
    Q_INVOKABLE void load_boxset_children(const QString &parentId);
    Q_INVOKABLE void load_folder_children(const QString &parentId);
    Q_INVOKABLE void load_next_episode(const QString &currentItemId);
    Q_INVOKABLE void build_stream_url(const QString &itemId, const QString &mediaSourceId = QString());
    Q_INVOKABLE void request_playback(const QString &itemId, const QString &mediaSourceId,
                                      int audioStreamIndex, int subtitleStreamIndex,
                                      bool forceTranscode = false, qint64 startPositionTicks = 0);
    Q_INVOKABLE void update_playback_progress(const QString &itemId, const QString &mediaSourceId,
                                              qint64 positionTicks, bool isPaused = false);
    Q_INVOKABLE void report_playback_stopped(const QString &itemId, const QString &mediaSourceId,
                                             qint64 positionTicks, bool failed = false);
    Q_INVOKABLE void fetchSegments(const QString &itemId);
    Q_INVOKABLE void probeCapabilities();

    Q_INVOKABLE QString get_last_audio_language() const { return m_lastAudioLanguage; }
    Q_INVOKABLE QString get_last_subtitle_language() const { return m_lastSubtitleLanguage; }
    Q_INVOKABLE void set_last_track_languages(const QString &audioLanguage,
                                               const QString &subtitleLanguage);

    Q_INVOKABLE void getVideoQualities();
    Q_INVOKABLE void getLibraries();
    Q_INVOKABLE void get_resume_playback_options();

signals:
    void authSuccess();
    void quickConnectCodeReady(const QString &code);
    void logoutComplete();
    void authStateChanged();
    void librariesLoaded(const QVariant &libraries);
    void itemsLoaded(const QVariant &items);
    void itemsPageLoaded(const QVariant &items, bool reset, bool finished);
    void watchHistoryChanged();
    void itemLoaded(const QVariant &detail);
    void streamUrlReady(const QString &url, const QVariant &httpHeaders);
    void nextEpisodeReady(const QVariantMap &detail);
    void segmentsReady(const QString &itemId, const QVariantList &segments);
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void errorOccurred(const QString &message);

private:
    QJsonObject loadAuth() const;
    void saveAuth(const QJsonObject &auth) const;
    QString authFilePath() const;
    QString deviceId() const;

    QString normalizedServerUrl(const QString &serverUrl) const;
    QString serverUrl() const;
    QString accessToken() const;
    QString userId() const;
    QString authorizationValue(const QString &token) const;

    QNetworkRequest jellyfinRequest(const QUrl &url, const QString &token = QString(),
                                    bool includeSavedToken = true) const;
    QNetworkReply *jellyfinGet(const QUrl &url);
    QNetworkReply *jellyfinPostJson(const QUrl &url, const QByteArray &body,
                                     const QString &token = QString(),
                                     bool includeSavedToken = true);

    void handleAuthFailure(const QString &message);
    bool saveAuthenticationResult(const QJsonObject &result, const QString &baseUrl);
    void pollQuickConnect();
    void authenticateWithQuickConnectSecret();
    void invalidateItemCache();
    void load_items_page(const QString &parentId, const QString &itemType, bool recursive,
                         int startIndex, int generation);
    void load_special_items(const QUrl &url, const QString &errorPrefix);
    void report_playback_start(const QString &itemId, const QString &mediaSourceId,
                               int audioStreamIndex, int subtitleStreamIndex,
                               qint64 startPositionTicks);
    QJsonObject moduleConfig() const;
    int videoQualityBitrate() const;
    int videoQualityMaxHeight() const;
    QVariantMap formatLibrary(const QJsonObject &item) const;
    QVariantMap formatItem(const QJsonObject &item) const;
    static int ticksToMs(qint64 ticks);
    static QString itemTitle(const QJsonObject &item);
    static QString episodeCode(const QJsonObject &item);

    QString m_dataRoot;
    QNetworkAccessManager *m_nam;
    QTimer *m_quickConnectTimer;
    QString m_quickConnectServerUrl;
    QString m_quickConnectSecret;
    QString m_currentPlaySessionId;
    QString m_currentPlayMethod;
    QString m_lastAudioLanguage;
    QString m_lastSubtitleLanguage = QStringLiteral("__off__");
    bool m_capabilitiesProbed = false;
    bool m_hasMediaSegments = false;
    int m_itemsLoadGeneration = 0;
    QVariantList m_itemsLoadAccumulator;
    QHash<QString, QVariantList> m_itemCache;
};
