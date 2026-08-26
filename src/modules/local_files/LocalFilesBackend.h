#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class LocalFilesBackend final : public QObject {
    Q_OBJECT

public:
    explicit LocalFilesBackend(const QString &appRoot, const QString &dataRoot,
                               QObject *parent = nullptr);
    ~LocalFilesBackend() override;

    Q_INVOKABLE QVariantList getItems(const QString &path);
    Q_INVOKABLE QString mediaRoot() const;
    Q_INVOKABLE void setMediaRoot(const QString &path);
    Q_INVOKABLE bool isImage(const QString &path) const;
    Q_INVOKABLE bool isAudio(const QString &path) const;
    Q_INVOKABLE bool isPlaylist(const QString &path) const;
    Q_INVOKABLE bool playlistContainsImages(const QString &path) const;

    Q_INVOKABLE QVariantMap getSavedPosition(const QString &filePath);
    Q_INVOKABLE void savePosition(const QString &filePath, int positionMs, int playlistPos);
    Q_INVOKABLE void clearPosition(const QString &filePath);
    Q_INVOKABLE QVariantMap probeMediaTracks(const QString &filePath);

    // Both persistent queues share one validated and atomically-written store.
    Q_INVOKABLE QVariantList getQueue(const QString &kind = QStringLiteral("media")) const;
    Q_INVOKABLE int enqueue(const QString &kind, const QVariantMap &candidate);
    Q_INVOKABLE bool removeQueueEntry(const QString &kind, const QString &entryId);
    Q_INVOKABLE bool moveQueueEntry(const QString &kind, int fromIndex, int toIndex);
    Q_INVOKABLE void clearQueue(const QString &kind);
    Q_INVOKABLE bool failQueueEntry(const QString &kind, const QString &entryId,
                                    const QString &message);
    Q_INVOKABLE bool resetQueueEntry(const QString &kind, const QString &entryId);
    Q_INVOKABLE QVariantMap preparePlayback(const QString &startEntryId,
                                            bool shuffle = false);
    Q_INVOKABLE QStringList soundtrackPaths(bool shuffle = false) const;
    Q_INVOKABLE QVariantMap currentSoundtrack() const;
    Q_INVOKABLE bool importYouTubePlaylist(const QString &url);
    Q_INVOKABLE void cancelYouTubePlaylistImport();

    Q_INVOKABLE void startAudio(const QStringList &paths, bool shuffle = false);
    Q_INVOKABLE void stopAudio();

    Q_INVOKABLE void get_repeat_mode_options();
    Q_INVOKABLE void get_resume_playback_options();
    Q_INVOKABLE void get_shuffle_playback_options();
    Q_INVOKABLE void get_auto_subtitles_options();
    Q_INVOKABLE void get_subtitle_languages();
    Q_INVOKABLE void get_image_duration_options();

signals:
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void queueChanged(const QString &kind, const QVariant &items);
    void youtubePlaylistImportStarted();
    void youtubePlaylistImportFinished(int addedCount);
    void youtubePlaylistImportFailed(const QString &message);
    void audioPlaybackStarted();
    void audioPlaybackFailed(const QString &message);
    void audioTrackStarted(const QVariantMap &track);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key,
                          const QVariant &value);

private slots:
    void onAudioProcessFinished();
    void tryConnectAudioIpc();
    void onAudioIpcReadyRead();
    void onYouTubePlaylistProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    static constexpr int kQueueSchemaVersion = 2;
    static constexpr int kMaxQueueEntries = 1000;
    static constexpr int kMaxPlaylistDepth = 8;

    QString historyFilePath() const;
    QString queueFilePath() const;
    QString playbackPlaylistFilePath() const;
    QVariantMap loadHistory() const;
    void saveHistory(const QVariantMap &history);
    bool isPathWithinMediaRoot(const QString &path) const;
    bool queueKindAcceptsPath(const QString &kind, const QString &path) const;
    bool isValidQueueKind(const QString &kind) const;
    static bool isValidYouTubeVideoId(const QString &videoId);
    static QString canonicalYouTubeVideoUrl(const QString &videoId);
    static QString canonicalYouTubePlaylistUrl(const QString &input);
    static bool isCanonicalYouTubeVideoUrl(const QString &url);

    QVariantList &mutableQueue(const QString &kind);
    const QVariantList &queue(const QString &kind) const;
    int queueIndexForEntryId(const QString &kind, const QString &entryId) const;
    QVariantMap validatedQueueEntry(const QString &kind, const QVariantMap &candidate,
                                    bool createEntryId, bool requireFile) const;
    QStringList expandedPlaylistEntries(const QString &playlistPath,
                                        const QString &kind,
                                        QSet<QString> &visited,
                                        int depth) const;
    void loadQueues();
    bool saveQueues() const;
    bool publishQueues(const QString &changedKind);
    void pruneQueuesForMediaRoot();
    QString writePlaybackPlaylist(const QVariantList &entries) const;
    void consumeYouTubePlaylistOutput(bool includeRemainder = false);
    void consumeYouTubePlaylistLine(const QByteArray &line);
    void clearYouTubePlaylistProcess();

    void launchAudioProcess();
    void sendAudioCommand(const QJsonArray &command);
    QVariantMap buildCurrentSoundtrack() const;
    QString fallbackSoundtrackTitle(const QString &path) const;
    void publishCurrentSoundtrack(quint64 generation, quint64 fileSerial);

    QString m_appRoot;
    QString m_dataRoot;
    QString m_mediaRoot;
    QVariantList m_mediaQueue;
    QVariantList m_soundtrackQueue;

    QProcess *m_audioProcess = nullptr;
    QStringList m_audioPaths;
    bool m_audioShuffle = false;
    bool m_audioStopRequested = true;
    bool m_audioPlaybackReady = false;
    int m_audioRespawnCount = 0;
    quint64 m_audioGeneration = 0;
    quint64 m_audioFileSerial = 0;
    quint64 m_publishedAudioFileSerial = 0;
    int m_audioPlaylistPos = -1;
    QVariantMap m_audioMetadata;
    QString m_audioMediaTitle;
    QVariantMap m_currentSoundtrack;
    QLocalSocket *m_audioIpc = nullptr;
    QTimer *m_audioConnectTimer = nullptr;
    QString m_audioSocketPath;

    QProcess *m_youtubePlaylistProcess = nullptr;
    QByteArray m_youtubePlaylistOutputBuffer;
    QVariantList m_youtubePlaylistEntries;
    bool m_youtubePlaylistOutputOverflow = false;
    quint64 m_youtubePlaylistGeneration = 0;
};
