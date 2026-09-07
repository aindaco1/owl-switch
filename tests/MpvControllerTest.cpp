#include "player/MpvController.h"
#include "tools/HelperResolver.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool writeExecutable(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    if (file.write(contents) != contents.size())
        return false;
    file.close();
    return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner);
}

}

class MpvControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void youtubeModesValidateFormats_data();
    void youtubeModesValidateFormats();
    void repeatModesUseNarrowMpvArguments_data();
    void repeatModesUseNarrowMpvArguments();
    void muteAudioUsesNoAudioArgument();
    void trackSelectionPreservesLaunchMute();
    void rendersAndClearsTrackOverlay();
    void audioOnlyUsesIsolatedBoundedPlayers();
    void videoWindowPolicy_data();
    void videoWindowPolicy();
    void bundledVulkanDriverOverridesExternalDiscovery();
};

void MpvControllerTest::bundledVulkanDriverOverridesExternalDiscovery()
{
    QTemporaryDir root;
    QVERIFY(QDir().mkpath(root.filePath("bin")));
    QVERIFY(writeExecutable(root.filePath("bin/mpv"), "#!/bin/sh\nexit 0\n"));
    const bool wasSet = qEnvironmentVariableIsSet("VK_DRIVER_FILES");
    const QByteArray previous = qgetenv("VK_DRIVER_FILES");
    const auto restore = qScopeGuard([&] {
        if (wasSet) qputenv("VK_DRIVER_FILES", previous);
        else qunsetenv("VK_DRIVER_FILES");
    });
    qputenv("VK_DRIVER_FILES", "/missing-external-driver.json");
    QCOMPARE(HelperResolver::processEnvironment(root.path()).value("VK_DRIVER_FILES"),
             QString("/missing-external-driver.json"));
    QVERIFY(QDir().mkpath(root.filePath("vulkan/icd.d")));
    const QString manifest = root.filePath("vulkan/icd.d/MoltenVK_icd.json");
    QFile file(manifest);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{}\n"); // The bundle validator checks the real manifest contents.
    file.close();
    QCOMPARE(QDir::cleanPath(HelperResolver::processEnvironment(root.path()).value("VK_DRIVER_FILES")),
             manifest);
    QVERIFY(file.remove());
    QVERIFY(QFile::link(root.filePath("bin/mpv"), manifest));
    QCOMPARE(HelperResolver::processEnvironment(root.path()).value("VK_DRIVER_FILES"),
             QString("/missing-external-driver.json"));
}

void MpvControllerTest::audioOnlyUsesIsolatedBoundedPlayers()
{
    QTemporaryDir root;
    QVERIFY(QDir().mkpath(root.filePath("bin")));
    QVERIFY(writeExecutable(root.filePath("bin/yt-dlp"), "#!/bin/sh\nexit 0\n"));
    QVERIFY(QDir().mkpath(root.filePath("bin/_internal/certifi")));
    const QString caPath = root.filePath("bin/_internal/certifi/cacert.pem");
    QFile ca(caPath);
    QVERIFY(ca.open(QIODevice::WriteOnly));
    ca.write("fixture: packaging tests validate the actual PEM contents\n");
    ca.close();
    const QString marker = root.filePath("arguments");
    QVERIFY(writeExecutable(root.filePath("bin/mpv"),
        "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + marker.toUtf8() + "'\nsleep 10\n"));
    MpvController first(root.path(), nullptr, nullptr, true);
    MpvController second(root.path(), nullptr, nullptr, true);
    auto readArguments = [&] {
        QFile file(marker);
        if (!file.open(QIODevice::ReadOnly)) return QStringList{};
        return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    };
    first.setPlaybackDisplay(1, true);
    first.loadAndPlayWithOptions("https://cdn.freesound.org/previews/538/538001_6746400-hq.mp3",
        {{"audioOnly", true}, {"videoFilters", "scale=640:480"}});
    QTRY_VERIFY_WITH_TIMEOUT(readArguments().contains("--pause=yes"), 3000);
    const auto firstArgs = readArguments();
    QVERIFY(firstArgs.contains("--tls-ca-file=" + caPath));
    for (const QString &argument : {"--no-config", "--no-video", "--force-window=no", "--ytdl=no",
             "--tls-verify=yes", "--cache-on-disk=no", "--demuxer-lavf-o=max_redirects=0",
             "--keep-open=no", "--volume=0"})
        QVERIFY2(firstArgs.contains(argument), qPrintable(argument));
    for (const QString &argument : firstArgs) {
        QVERIFY(!argument.startsWith("--script="));
        QVERIFY(!argument.startsWith("--vf="));
        QVERIFY(!argument.startsWith("--screen="));
        QVERIFY(!argument.startsWith("--fs-screen="));
        QVERIFY(!argument.startsWith("--focus-on="));
        QVERIFY(!argument.startsWith("--border="));
        QVERIFY(!argument.startsWith("--geometry="));
        QVERIFY(!argument.startsWith("--macos-geometry-calculation="));
    }
    const QString firstSocket = firstArgs.filter("--input-ipc-server=").value(0);
    QVERIFY(QFile::remove(marker));
    second.loadAndPlayWithOptions("/test/second.mp3", {{"audioOnly", true}});
    QTRY_VERIFY_WITH_TIMEOUT(!readArguments().isEmpty(), 3000);
    const QString secondSocket = readArguments().filter("--input-ipc-server=").value(0);
    QVERIFY(!firstSocket.isEmpty() && !secondSocket.isEmpty());
    QVERIFY(firstSocket != secondSocket);
    first.stopImmediately();
    QVERIFY(!first.running());
    QVERIFY(second.running());
    second.stopImmediately();
    QVERIFY(!second.running());
}

void MpvControllerTest::videoWindowPolicy_data()
{
    QTest::addColumn<bool>("separate");
    QTest::newRow("separate media display") << true;
    QTest::newRow("same display") << false;
}

void MpvControllerTest::videoWindowPolicy()
{
    QFETCH(bool, separate);
    QTemporaryDir root;
    QVERIFY(QDir().mkpath(root.filePath("bin")));
    const QString marker = root.filePath("arguments");
    QVERIFY(writeExecutable(root.filePath("bin/mpv"),
        "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + marker.toUtf8() + "'\n"));
    MpvController controller(root.path(), nullptr, nullptr, true);
    controller.setPlaybackDisplay(separate ? 1 : 0, separate);
    controller.loadAndPlayWithOptions("/test/video.mp4");
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 3000);
    QFile file(marker);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto arguments = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    QVERIFY(arguments.contains("--fullscreen"));
    QVERIFY(arguments.contains("--no-native-fs"));
    QVERIFY(arguments.contains(QString("--screen=%1").arg(separate ? 1 : 0)));
    QCOMPARE(arguments.contains("--focus-on=never"), separate);
    QCOMPARE(arguments.contains("--border=no"), separate);
    QCOMPARE(arguments.contains("--geometry=100%x100%+0+0"), separate);
    QCOMPARE(arguments.contains("--macos-geometry-calculation=whole"), separate);
}

void MpvControllerTest::youtubeModesValidateFormats_data()
{
    QTest::addColumn<QString>("oscMode");
    QTest::addColumn<QString>("modeArgument");

    QTest::newRow("karaoke") << QStringLiteral("karaoke")
                              << QStringLiteral("--script-opts-append=karaoke=1");
    QTest::newRow("retro") << QStringLiteral("retro")
                            << QStringLiteral("--script-opts-append=retro-tv=1");
}

void MpvControllerTest::youtubeModesValidateFormats()
{
    QFETCH(QString, oscMode);
    QFETCH(QString, modeArgument);

    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));

    const QString markerPath = root.filePath(QStringLiteral("mpv-arguments.txt"));
    const QString fakeMpvPath = QDir(binDirectory).filePath(QStringLiteral("mpv"));
    const QByteArray fakeMpv = QByteArrayLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"")
                             + QFile::encodeName(markerPath)
                             + QByteArrayLiteral("\"\n");
    QVERIFY(writeExecutable(fakeMpvPath, fakeMpv));

    const QString fakeYtDlpPath = QDir(binDirectory).filePath(QStringLiteral("yt-dlp"));
    const QString fakeDenoPath = QDir(binDirectory).filePath(QStringLiteral("deno"));
    QVERIFY(writeExecutable(fakeYtDlpPath, QByteArrayLiteral("#!/bin/sh\nexit 0\n")));
    QVERIFY(writeExecutable(fakeDenoPath, QByteArrayLiteral("#!/bin/sh\nexit 0\n")));

    MpvController controller(appRoot);
    controller.setPlaybackDisplay(1, true);
    controller.loadAndPlay(QStringLiteral("https://www.youtube.com/watch?v=abcdefghijk"),
                           0.0f, 0, -1, QStringList{}, false, -1, 0.0f,
                           QString{}, false, oscMode);

    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        QFile pendingMarker(markerPath);
        if (!pendingMarker.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        const QStringList pendingArguments = QString::fromUtf8(pendingMarker.readAll())
                                                 .split('\n', Qt::SkipEmptyParts);
        return pendingArguments.contains(modeArgument)
            && pendingArguments.contains(QStringLiteral("--screen=1"))
            && pendingArguments.contains(QStringLiteral("--fs-screen=1"));
    }(), 3000);

    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList arguments = QString::fromUtf8(marker.readAll())
                                      .split('\n', Qt::SkipEmptyParts);

    QVERIFY(arguments.contains(modeArgument));
    QVERIFY(arguments.contains(QStringLiteral("--screen=1")));
    QVERIFY(arguments.contains(QStringLiteral("--fs-screen=1")));
    QVERIFY(arguments.contains(QStringLiteral(
        "--ytdl-raw-options-append=check-formats=")));
    QVERIFY(arguments.contains(QStringLiteral(
        "--script-opts-append=ytdl_hook-ytdl_path=") + fakeYtDlpPath));
    QVERIFY(arguments.contains(QStringLiteral(
        "--ytdl-raw-options-append=js-runtimes=deno:") + fakeDenoPath));
}

void MpvControllerTest::repeatModesUseNarrowMpvArguments_data()
{
    QTest::addColumn<QString>("repeatMode");
    QTest::addColumn<QString>("expectedArgument");
    QTest::newRow("queue") << QStringLiteral("queue") << QStringLiteral("--loop-playlist=inf");
    QTest::newRow("one") << QStringLiteral("one") << QStringLiteral("--loop-file=inf");
}

void MpvControllerTest::repeatModesUseNarrowMpvArguments()
{
    QFETCH(QString, repeatMode);
    QFETCH(QString, expectedArgument);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));
    const QString markerPath = root.filePath(QStringLiteral("mpv-arguments.txt"));
    const QByteArray fakeMpv = QByteArrayLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"")
        + QFile::encodeName(markerPath) + QByteArrayLiteral("\"\n");
    QVERIFY(writeExecutable(QDir(binDirectory).filePath(QStringLiteral("mpv")), fakeMpv));

    MpvController controller(appRoot);
    controller.loadAndPlayWithOptions(QStringLiteral("/tmp/local-queue.m3u8"),
                                      {{QStringLiteral("repeatMode"), repeatMode}});
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        QFile pendingMarker(markerPath);
        if (!pendingMarker.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        return QString::fromUtf8(pendingMarker.readAll())
            .split('\n', Qt::SkipEmptyParts)
            .contains(expectedArgument);
    }(), 3000);
    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList arguments = QString::fromUtf8(marker.readAll())
                                      .split('\n', Qt::SkipEmptyParts);
    QVERIFY(arguments.contains(expectedArgument));
    if (repeatMode == QLatin1String("one"))
        QVERIFY(!arguments.contains(QStringLiteral("--loop-playlist=inf")));
}

void MpvControllerTest::muteAudioUsesNoAudioArgument()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));
    const QString markerPath = root.filePath(QStringLiteral("mpv-arguments.txt"));
    const QByteArray fakeMpv = QByteArrayLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"")
        + QFile::encodeName(markerPath) + QByteArrayLiteral("\"\n");
    QVERIFY(writeExecutable(QDir(binDirectory).filePath(QStringLiteral("mpv")), fakeMpv));

    MpvController controller(appRoot);
    controller.loadAndPlayWithOptions(QStringLiteral("/tmp/local-queue.m3u8"),
                                      {{QStringLiteral("muteAudio"), true}});
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(markerPath), 3000);
    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList arguments = QString::fromUtf8(marker.readAll())
                                      .split('\n', Qt::SkipEmptyParts);
    QVERIFY(arguments.contains(QStringLiteral("--no-audio")));
}

void MpvControllerTest::trackSelectionPreservesLaunchMute()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));

    const QString markerPath = root.filePath(QStringLiteral("ipc-commands.jsonl"));
    const QByteArray fakeMpv = QByteArrayLiteral(
        "#!/bin/sh\n"
        "socket_path=''\n"
        "for argument in \"$@\"; do\n"
        "  case \"$argument\" in\n"
        "    --input-ipc-server=*) socket_path=${argument#*=} ;;\n"
        "  esac\n"
        "done\n"
        "exec /usr/bin/python3 - \"$socket_path\" \"")
        + QFile::encodeName(markerPath)
        + QByteArrayLiteral("\" <<'PY'\n"
            "import json, os, socket, sys, time\n"
            "socket_path, marker_path = sys.argv[1:3]\n"
            "try:\n"
            "    os.unlink(socket_path)\n"
            "except FileNotFoundError:\n"
            "    pass\n"
            "server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
            "server.bind(socket_path)\n"
            "server.listen(1)\n"
            "connection, _ = server.accept()\n"
            "connection.settimeout(5)\n"
            "connection.sendall(b'{\"event\":\"property-change\",\"name\":\"playlist-pos\",\"data\":0}\\n')\n"
            "connection.sendall(b'{\"event\":\"file-loaded\"}\\n')\n"
            "buffer = b''\n"
            "deadline = time.time() + 5\n"
            "with open(marker_path, 'ab', buffering=0) as marker:\n"
            "    while time.time() < deadline:\n"
            "        try:\n"
            "            chunk = connection.recv(4096)\n"
            "        except socket.timeout:\n"
            "            continue\n"
            "        if not chunk:\n"
            "            break\n"
            "        buffer += chunk\n"
            "        while b'\\n' in buffer:\n"
            "            line, buffer = buffer.split(b'\\n', 1)\n"
            "            marker.write(line + b'\\n')\n"
            "            try:\n"
            "                command = json.loads(line).get('command', [])\n"
            "            except Exception:\n"
            "                command = []\n"
            "            if isinstance(command, list) and command[:2] == ['set_property', 'aid']:\n"
            "                time.sleep(0.1)\n"
            "                sys.exit(0)\n"
            "sys.exit(1)\n"
            "PY\n");
    QVERIFY(writeExecutable(QDir(binDirectory).filePath(QStringLiteral("mpv")), fakeMpv));

    MpvController controller(appRoot);
    QSignalSpy loadedSpy(&controller, &MpvController::playbackItemLoaded);
    controller.loadAndPlayWithOptions(QStringLiteral("/tmp/local-queue.m3u8"),
                                      {{QStringLiteral("muteAudio"), true}});
    QTRY_COMPARE_WITH_TIMEOUT(loadedSpy.count(), 1, 5000);

    const QString replacementUrl =
        QStringLiteral("https://www.youtube.com/watch?v=abcdefghijk");
    QVERIFY(!controller.replaceCurrentMedia(QString{}));
    QVERIFY(controller.replaceCurrentMedia(replacementUrl));
    const auto markerContainsReplacement = [&markerPath, &replacementUrl] {
        QFile marker(markerPath);
        if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        while (!marker.atEnd()) {
            const QJsonArray command = QJsonDocument::fromJson(marker.readLine())
                                           .object().value(QStringLiteral("command")).toArray();
            if (command.size() >= 3 &&
                command.at(0).toString() == QLatin1String("loadfile") &&
                command.at(1).toString() == replacementUrl &&
                command.at(2).toString() == QLatin1String("replace")) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(markerContainsReplacement(), 5000);

    controller.selectPlaybackTracks(1, -2, {});
    const auto markerContainsMutedSelection = [&markerPath] {
        QFile marker(markerPath);
        if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        while (!marker.atEnd()) {
            const QJsonArray command = QJsonDocument::fromJson(marker.readLine())
                                           .object().value(QStringLiteral("command")).toArray();
            if (command.size() >= 3 &&
                command.at(0).toString() == QLatin1String("set_property") &&
                command.at(1).toString() == QLatin1String("aid") &&
                command.at(2).toString() == QLatin1String("no")) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(markerContainsMutedSelection(), 5000);
}

void MpvControllerTest::rendersAndClearsTrackOverlay()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString appRoot = root.filePath(QStringLiteral("app"));
    const QString binDirectory = QDir(appRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(binDirectory));

    const QString markerPath = root.filePath(QStringLiteral("ipc-commands.jsonl"));
    const QByteArray fakeMpv = QByteArrayLiteral(
        "#!/bin/sh\n"
        "socket_path=''\n"
        "for argument in \"$@\"; do\n"
        "  case \"$argument\" in\n"
        "    --input-ipc-server=*) socket_path=${argument#*=} ;;\n"
        "  esac\n"
        "done\n"
        "exec /usr/bin/python3 - \"$socket_path\" \"")
        + QFile::encodeName(markerPath)
        + QByteArrayLiteral("\" <<'PY'\n"
            "import os, socket, sys, time\n"
            "socket_path, marker_path = sys.argv[1:3]\n"
            "try:\n"
            "    os.unlink(socket_path)\n"
            "except FileNotFoundError:\n"
            "    pass\n"
            "server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
            "server.bind(socket_path)\n"
            "server.listen(1)\n"
            "connection, _ = server.accept()\n"
            "connection.settimeout(5)\n"
            "connection.sendall(b'{\"event\":\"property-change\",\"name\":\"path\",\"data\":\"/tmp/video.mp4\"}\\n')\n"
            "connection.sendall(b'{\"event\":\"file-loaded\"}\\n')\n"
            "buffer = b''\n"
            "deadline = time.time() + 5\n"
            "with open(marker_path, 'ab', buffering=0) as marker:\n"
            "    while time.time() < deadline:\n"
            "        try:\n"
            "            chunk = connection.recv(4096)\n"
            "        except socket.timeout:\n"
            "            continue\n"
            "        if not chunk:\n"
            "            break\n"
            "        buffer += chunk\n"
            "        while b'\\n' in buffer:\n"
            "            line, buffer = buffer.split(b'\\n', 1)\n"
            "            marker.write(line + b'\\n')\n"
            "PY\n");
    QVERIFY(writeExecutable(QDir(binDirectory).filePath(QStringLiteral("mpv")), fakeMpv));

    MpvController controller(appRoot);
    QSignalSpy loadedSpy(&controller, &MpvController::playbackItemLoaded);
    controller.loadAndPlayWithOptions(QStringLiteral("/tmp/video.mp4"));
    QTRY_COMPARE_WITH_TIMEOUT(loadedSpy.count(), 1, 5000);
    QCOMPARE(controller.currentPath(), QStringLiteral("/tmp/video.mp4"));

    controller.showTrackOverlay(
        {{QStringLiteral("displayTitle"), QStringLiteral("Ar{t}\\ist - So}ng")},
         {QStringLiteral("fallbackArtist"), QStringLiteral("KARAOKE")}},
        QStringLiteral("UP {NEXT}"), 100);

    const auto overlayCommands = [&markerPath] {
        QList<QJsonObject> commands;
        QFile marker(markerPath);
        if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
            return commands;
        while (!marker.atEnd()) {
            const QJsonValue command = QJsonDocument::fromJson(marker.readLine())
                                           .object().value(QStringLiteral("command"));
            if (command.isObject() &&
                command.toObject().value(QStringLiteral("name")).toString() ==
                    QLatin1String("osd-overlay")) {
                commands.append(command.toObject());
            }
        }
        return commands;
    };
    QTRY_VERIFY_WITH_TIMEOUT(overlayCommands().size() >= 2, 5000);
    const QList<QJsonObject> commands = overlayCommands();
    const QJsonObject shown = commands.at(commands.size() - 2);
    const QJsonObject cleared = commands.last();
    QCOMPARE(shown.value(QStringLiteral("format")).toString(), QStringLiteral("ass-events"));
    QCOMPARE(shown.value(QStringLiteral("res_x")).toInt(), 1920);
    QCOMPARE(shown.value(QStringLiteral("res_y")).toInt(), 1080);
    const QString data = shown.value(QStringLiteral("data")).toString();
    QVERIFY(data.contains(QStringLiteral("Artist")));
    QVERIFY(data.contains(QStringLiteral("Song")));
    QVERIFY(data.contains(QStringLiteral("UP NEXT")));
    QVERIFY(data.contains(QStringLiteral("\\an5\\pos(1476,911)")));
    QVERIFY(data.contains(QStringLiteral("m 0 0 l 760 0 760 210 0 210")));
    QVERIFY(data.contains(QStringLiteral("\\fs42")));
    QVERIFY(data.contains(QStringLiteral("\\fs34")));
    QVERIFY(!data.contains(QStringLiteral("Ar{t}")));
    QVERIFY(!data.contains(QStringLiteral("So}ng")));
    QCOMPARE(cleared.value(QStringLiteral("format")).toString(), QStringLiteral("none"));

    controller.showTrackOverlay(
        {{QStringLiteral("artist"), QStringLiteral("Persistent Artist")},
         {QStringLiteral("song"), QStringLiteral("Persistent Song")}},
        QString{}, 0);
    QTest::qWait(250);
    QCOMPARE(overlayCommands().last().value(QStringLiteral("format")).toString(),
             QStringLiteral("ass-events"));
    controller.clearTrackOverlay();
    QTRY_COMPARE_WITH_TIMEOUT(
        overlayCommands().last().value(QStringLiteral("format")).toString(),
        QStringLiteral("none"), 1000);
}

QTEST_GUILESS_MAIN(MpvControllerTest)
#include "MpvControllerTest.moc"
