#include "player/AudioCrossfadePlayer.h"
#include "player/MpvController.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

class AudioCrossfadePlayerTest final : public QObject {
    Q_OBJECT
private slots:
    void gainsKeepEnergyAndHeadroom();
    void repeatsCrossfadesAndStopsBothPlayers();
    void missingHelperStopsAfterBoundedFailures();
    void realDecoderRotatesToNaturalEnd();
};

void AudioCrossfadePlayerTest::gainsKeepEnergyAndHeadroom()
{
    QCOMPARE(AudioCrossfadePlayer::fadeGain(0, true), 0.0);
    QVERIFY(AudioCrossfadePlayer::fadeGain(1, false) < 0.000001);
    for (int i = 0; i <= 100; ++i) {
        const double incoming = AudioCrossfadePlayer::fadeGain(i / 100.0, true);
        const double outgoing = AudioCrossfadePlayer::fadeGain(i / 100.0, false);
        QVERIFY(std::abs(incoming * incoming + outgoing * outgoing - 1) < 0.000001);
        QVERIFY(0.7 * (incoming + outgoing) < 1.0);
        const double volume = AudioCrossfadePlayer::mpvVolume(30, incoming) / 100;
        QVERIFY(std::abs(std::pow(volume, 3) - 0.3 * 0.7 * incoming) < 0.000001);
    }
}

void AudioCrossfadePlayerTest::realDecoderRotatesToNaturalEnd()
{
    const QString mpv = qEnvironmentVariable("NATURE_AUDIO_REAL_MPV");
    if (mpv.isEmpty())
        QSKIP("Set NATURE_AUDIO_REAL_MPV to a real helper for the 30-second decoder test");
    QVERIFY(QFileInfo(mpv).isExecutable());
    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    QVERIFY(!ffmpeg.isEmpty());
    QTemporaryDir root;
    QVERIFY(QDir().mkpath(root.filePath("bin")));
    QFile wrapper(root.filePath("bin/mpv"));
    QVERIFY(wrapper.open(QIODevice::WriteOnly));
    QString quoted = mpv;
    quoted.replace("'", "'\"'\"'");
    wrapper.write(("#!/bin/sh\nexec '" + quoted + "' --ao=null \"$@\"\n").toUtf8());
    wrapper.close();
    QVERIFY(wrapper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QVariantList catalog;
    for (int i = 0; i < 3; ++i) {
        const QString path = root.filePath(QString("tone%1.mp3").arg(i));
        QCOMPARE(QProcess::execute(ffmpeg, {"-v", "error", "-f", "lavfi", "-i",
            QString("sine=frequency=%1:duration=12").arg(220 + i * 110), "-c:a", "libmp3lame", path}), 0);
        catalog.append(QVariantMap{{"id", QString::number(i)}, {"previewUrl", path}});
    }
    AudioCrossfadePlayer player(root.path());
    QSignalSpy tracks(&player, &AudioCrossfadePlayer::trackChanged);
    QSignalSpy failed(&player, &AudioCrossfadePlayer::unavailable);
    int eof = 0;
    for (const auto &deck : player.m_decks) {
        connect(deck.player, &MpvController::playbackItemEnded, &player,
                [&](int, const QString &reason, const QString &) { if (reason == "eof") ++eof; });
    }
    player.start(catalog);
    QTRY_VERIFY_WITH_TIMEOUT(tracks.count() >= 2, 12000);
    QVERIFY(player.m_incoming >= 0);
    QTRY_VERIFY_WITH_TIMEOUT(player.m_decks[0].gain > 0 && player.m_decks[1].gain > 0, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(eof >= 3, 30000);
    QVERIFY(tracks.count() >= 4);
    QCOMPARE(failed.count(), 0);
    player.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!player.active(), 1000);
    for (const auto &deck : player.m_decks)
        QVERIFY(!deck.player->running());
}

void AudioCrossfadePlayerTest::repeatsCrossfadesAndStopsBothPlayers()
{
    QTemporaryDir root;
    QVERIFY(QDir().mkpath(root.filePath("bin")));
    QFile fixture(QStringLiteral(TEST_SOURCE_ROOT "/tests/fixtures/fake-mpv-audio.py"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    QFile helper(root.filePath("bin/mpv"));
    QVERIFY(helper.open(QIODevice::WriteOnly));
    helper.write(fixture.readAll());
    helper.close();
    QVERIFY(helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    AudioCrossfadePlayer player(root.path());
    QSignalSpy tracks(&player, &AudioCrossfadePlayer::trackChanged);
    player.start({QVariantMap{{"id", "1"}, {"previewUrl", "/test/one.mp3"}},
                  QVariantMap{{"id", "2"}, {"previewUrl", "/test/two.mp3"}},
                  QVariantMap{{"id", "3"}, {"previewUrl", "/test/three.mp3"}}});
    QTRY_VERIFY_WITH_TIMEOUT(tracks.count() >= 2, 5000);
    QVERIFY(player.m_incoming >= 0);
    for (const auto &deck : player.m_decks)
        QVERIFY(deck.player->running());
    player.setPaused(true);
    QTest::qWait(150);
    const int position = player.m_decks[player.m_current].player->position();
    QTest::qWait(250);
    QCOMPARE(player.m_decks[player.m_current].player->position(), position);
    player.setVolume(50);
    player.setPaused(false);
    QTRY_VERIFY_WITH_TIMEOUT(tracks.count() >= 4, 10000);
    QSet<QString> firstCycle;
    for (int i = 0; i < 3; ++i)
        firstCycle.insert(tracks[i][0].toMap().value("id").toString());
    QCOMPARE(firstCycle.size(), 3);
    QVERIFY(tracks[2][0].toMap().value("id") != tracks[3][0].toMap().value("id"));
    player.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!player.active(), 1000);
    for (const auto &deck : player.m_decks)
        QVERIFY(!deck.player->running());
    const int count = tracks.count();
    QTest::qWait(300);
    QCOMPARE(tracks.count(), count);
}

void AudioCrossfadePlayerTest::missingHelperStopsAfterBoundedFailures()
{
    QTemporaryDir root;
    QVERIFY(QDir().mkpath(root.filePath("bin")));
    QFile helper(root.filePath("bin/mpv"));
    QVERIFY(helper.open(QIODevice::WriteOnly));
    helper.write("#!/bin/sh\nexit 1\n");
    helper.close();
    helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    AudioCrossfadePlayer player(root.path());
    QSignalSpy failed(&player, &AudioCrossfadePlayer::unavailable);
    player.start({QVariantMap{{"id", "1"}, {"previewUrl", "/test/missing.mp3"}}});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(!player.active(), 1000);
}

QTEST_GUILESS_MAIN(AudioCrossfadePlayerTest)
#include "AudioCrossfadePlayerTest.moc"
