#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantList>
#include <array>

class MpvController;
class AudioCrossfadePlayerTest;

// Two bounded, isolated instances of the shared mpv handoff. Callers validate
// their own media identities; this class owns only playback and transitions.
class AudioCrossfadePlayer final : public QObject {
    Q_OBJECT
public:
    explicit AudioCrossfadePlayer(const QString &appRoot, QObject *parent = nullptr);
    ~AudioCrossfadePlayer() override;
    void start(const QVariantList &tracks);
    void stop();
    void setPaused(bool paused);
    void setVolume(int volume);
    bool active() const { return m_active || m_stopping; }
    static double fadeGain(double progress, bool incoming);
    static double mpvVolume(int master, double gain);

signals:
    void trackChanged(const QVariantMap &track);
    void unavailable();
    void activeChanged();

private:
    friend class AudioCrossfadePlayerTest;
    enum class State { Empty, Loading, Ready, Playing };
    struct Deck {
        MpvController *player = nullptr;
        State state = State::Empty;
        QVariantMap track;
        QElapsedTimer deadline;
        QElapsedTimer progress;
        int lastPosition = -1;
        double gain = 0;
    };
    void tick();
    void prepare(int index);
    void play(int index);
    void ended(int index, bool failed);
    void clear(int index);
    void stopNow();
    void applyGain(int index, double gain);
    void refillDeck();

    std::array<Deck, 2> m_decks;
    QVariantList m_tracks;
    QList<int> m_order;
    QString m_lastId;
    QTimer m_timer;
    QElapsedTimer m_stopTime;
    QElapsedTimer m_lastFailure;
    std::array<double, 2> m_stopGains{{0, 0}};
    int m_current = -1;
    int m_incoming = -1;
    int m_overlapMs = 5000;
    int m_failures = 0;
    int m_volume = 30;
    bool m_paused = false;
    bool m_active = false;
    bool m_stopping = false;
};
