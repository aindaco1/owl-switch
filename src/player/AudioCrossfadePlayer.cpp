#include "AudioCrossfadePlayer.h"
#include "MpvController.h"
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kFadeMs = 5000;
constexpr int kDeadlineMs = 20000;
constexpr int kMaximumFailures = 5;
constexpr double kHeadroom = 0.7;
}

AudioCrossfadePlayer::AudioCrossfadePlayer(const QString &appRoot, QObject *parent)
    : QObject(parent)
{
    for (int i = 0; i < 2; ++i) {
        Deck &deck = m_decks[i];
        deck.player = new MpvController(appRoot, nullptr, this, true);
        connect(deck.player, &MpvController::playbackReady, this, [this, i] {
            if (m_active && m_decks[i].state == State::Loading)
                m_decks[i].state = State::Ready;
        });
        connect(deck.player, &MpvController::playbackItemEnded, this,
                [this, i](int, const QString &reason, const QString &) {
            if (reason == QLatin1String("eof") || reason == QLatin1String("error"))
                ended(i, reason == QLatin1String("error"));
        });
        connect(deck.player, &MpvController::playbackEnded, this,
                [this, i](int, int, const QString &) {
            ended(i, true);
        });
    }
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &AudioCrossfadePlayer::tick);
}

AudioCrossfadePlayer::~AudioCrossfadePlayer() { stopNow(); }

double AudioCrossfadePlayer::fadeGain(double progress, bool incoming)
{
    const double angle = qBound(0.0, progress, 1.0) * 1.5707963267948966;
    return incoming ? std::sin(angle) : std::cos(angle);
}

double AudioCrossfadePlayer::mpvVolume(int master, double gain)
{
    // mpv cubes volume/100 in player/audio.c. Invert it so our envelope and
    // master percentage describe linear amplitude, including overlap headroom.
    return 100.0 * std::cbrt(qBound(0, master, 100) / 100.0 * kHeadroom *
                           qBound(0.0, gain, 1.0));
}

void AudioCrossfadePlayer::start(const QVariantList &tracks)
{
    stopNow();
    m_tracks = tracks.mid(0, 2500);
    if (m_tracks.isEmpty()) {
        emit unavailable();
        return;
    }
    m_active = true;
    m_failures = 0;
    m_lastFailure.invalidate();
    m_lastId.clear();
    m_current = 0;
    prepare(0);
    m_timer.start();
    emit activeChanged();
}

void AudioCrossfadePlayer::refillDeck()
{
    for (int i = 0; i < m_tracks.size(); ++i)
        m_order.append(i);
    std::shuffle(m_order.begin(), m_order.end(), *QRandomGenerator::global());
    if (m_order.size() > 1 &&
        m_tracks[m_order.last()].toMap().value("id").toString() == m_lastId)
        m_order.swapItemsAt(0, m_order.size() - 1);
}

void AudioCrossfadePlayer::prepare(int index)
{
    if (!m_active || m_failures >= kMaximumFailures)
        return;
    if (m_failures > 0 && m_lastFailure.isValid() &&
        m_lastFailure.elapsed() < qMin(4000, 250 * (1 << (m_failures - 1))))
        return;
    if (m_order.isEmpty())
        refillDeck();
    Deck &deck = m_decks[index];
    clear(index);
    deck.track = m_tracks[m_order.takeLast()].toMap();
    m_lastId = deck.track.value("id").toString();
    deck.state = State::Loading;
    deck.deadline.start();
    deck.player->loadAndPlayWithOptions(deck.track.value("previewUrl").toString(),
        {{"audioOnly", true}, {"paused", true}, {"volume", 0}});
}

void AudioCrossfadePlayer::play(int index)
{
    Deck &deck = m_decks[index];
    deck.state = State::Playing;
    deck.progress.start();
    deck.player->setPaused(m_paused);
    emit trackChanged(deck.track);
}

void AudioCrossfadePlayer::clear(int index)
{
    Deck &deck = m_decks[index];
    deck.state = State::Empty;
    deck.player->stopImmediately();
    deck.track.clear();
    deck.gain = 0;
    deck.lastPosition = -1;
}

void AudioCrossfadePlayer::stopNow()
{
    const bool wasActive = active();
    m_active = false;
    m_stopping = false;
    m_timer.stop();
    for (int i = 0; i < 2; ++i)
        clear(i);
    m_current = m_incoming = -1;
    m_order.clear();
    m_tracks.clear();
    if (wasActive)
        emit activeChanged();
}

void AudioCrossfadePlayer::stop()
{
    if (!m_active || m_stopping)
        return;
    m_active = false;
    m_stopping = true;
    m_stopTime.start();
    for (int i = 0; i < 2; ++i) {
        m_stopGains[i] = m_decks[i].gain;
        if (m_decks[i].state != State::Playing)
            clear(i);
    }
}

void AudioCrossfadePlayer::setPaused(bool paused)
{
    m_paused = paused;
    for (Deck &deck : m_decks) {
        if (deck.state == State::Playing)
            deck.player->setPaused(paused);
    }
}

void AudioCrossfadePlayer::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    for (int i = 0; i < 2; ++i)
        applyGain(i, m_decks[i].gain);
}

void AudioCrossfadePlayer::applyGain(int index, double gain)
{
    Deck &deck = m_decks[index];
    deck.gain = qBound(0.0, gain, 1.0);
    if (deck.state == State::Playing)
        deck.player->setVolume(mpvVolume(m_volume, deck.gain));
}

void AudioCrossfadePlayer::ended(int index, bool failed)
{
    if (!m_active || m_decks[index].state == State::Empty)
        return;
    if (failed) {
        ++m_failures;
        m_lastFailure.start();
    } else {
        m_failures = 0;
        m_lastFailure.invalidate();
    }
    clear(index);
    if (index == m_incoming)
        m_incoming = -1;
    if (index == m_current) {
        const int other = 1 - index;
        m_current = other;
        m_incoming = -1;
        if (m_decks[other].state == State::Ready)
            play(other);
        else if (m_decks[other].state == State::Empty)
            prepare(other);
    }
    if (m_failures >= kMaximumFailures) {
        stop();
        emit unavailable();
    }
}

void AudioCrossfadePlayer::tick()
{
    if (m_stopping) {
        const double progress = m_stopTime.elapsed() / 200.0;
        for (int i = 0; i < 2; ++i)
            applyGain(i, m_stopGains[i] * fadeGain(progress, false));
        if (progress >= 1.0)
            stopNow();
        return;
    }
    if (!m_active || m_current < 0)
        return;
    for (int i = 0; i < 2; ++i) {
        Deck &deck = m_decks[i];
        if (deck.state == State::Loading && deck.deadline.elapsed() > kDeadlineMs) {
            ended(i, true);
            return;
        }
        if (deck.state == State::Playing) {
            const int position = deck.player->position();
            // Do not trust catalog durations to bound a malformed media stream.
            if (position > 610000) {
                ended(i, true);
                return;
            }
            if (m_paused || position != deck.lastPosition) {
                deck.lastPosition = position;
                deck.progress.restart();
            } else if (deck.progress.elapsed() > kDeadlineMs) {
                ended(i, true);
                return;
            }
        }
    }
    Deck &current = m_decks[m_current];
    if (current.state == State::Empty)
        prepare(m_current);
    if (current.state == State::Ready)
        play(m_current);
    if (current.state != State::Playing)
        return;
    const int other = 1 - m_current;
    if (m_decks[other].state == State::Empty)
        prepare(other);
    if (m_paused)
        return;

    const int duration = current.player->duration();
    const int position = current.player->position();
    const int fadeMs = qMin(kFadeMs, qMax(1, duration / 2));
    const int remaining = qMax(0, duration - position);
    if (duration > 0 && remaining <= fadeMs && m_incoming < 0 &&
        m_decks[other].state == State::Ready) {
        m_incoming = other;
        m_overlapMs = qMax(1, remaining);
        play(other);
    }
    if (m_incoming >= 0) {
        const double progress = qMax(m_decks[m_incoming].player->position(),
                                     m_overlapMs - remaining) / double(m_overlapMs);
        applyGain(m_current, fadeGain(progress, false));
        applyGain(m_incoming, fadeGain(progress, true));
    } else {
        double gain = fadeGain(position / double(kFadeMs), true);
        if (duration > 0 && remaining < fadeMs)
            gain = qMin(gain, fadeGain(1.0 - remaining / double(fadeMs), false));
        applyGain(m_current, gain);
    }
}
