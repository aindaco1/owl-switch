#include "UpdateManager.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace {
constexpr auto kReleaseApi = "https://api.github.com/repos/" APP_UPDATE_REPOSITORY "/releases/latest";
constexpr auto kBundleIdentifier = APP_BUNDLE_IDENTIFIER;
constexpr auto kExpectedTeamIdentifier = APP_UPDATE_TEAM_ID;
constexpr auto kPrimaryBundleName = APP_BUNDLE_NAME;
constexpr auto kLegacyBundleName = APP_LEGACY_BUNDLE_NAME;

struct MountedImage {
    QString mountPoint;
    QString appPath;
};

QString processOutput(const QString &program, const QStringList &arguments, int *exitCode = nullptr) {
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        if (exitCode)
            *exitCode = -1;
        return {};
    }
    if (exitCode)
        *exitCode = process.exitCode();
    return QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
}

MountedImage mountDiskImage(const QString &path, QString *errorMessage) {
    int exitCode = -1;
    const QString output = processOutput(QStringLiteral("/usr/bin/hdiutil"),
        {QStringLiteral("attach"), QStringLiteral("-nobrowse"), QStringLiteral("-readonly"),
         QStringLiteral("-noautoopen"), path}, &exitCode);
    if (exitCode != 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The downloaded disk image could not be mounted.");
        return {};
    }

    QString mountPoint;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const int volumeIndex = it->indexOf(QStringLiteral("/Volumes/"));
        if (volumeIndex >= 0) {
            mountPoint = it->mid(volumeIndex).trimmed();
            break;
        }
    }
    if (mountPoint.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The downloaded disk image did not expose a volume.");
        return {};
    }

    QString appPath = QDir(mountPoint).filePath(QString::fromLatin1(kPrimaryBundleName));
    if (!QDir(appPath).exists())
        appPath = QDir(mountPoint).filePath(QString::fromLatin1(kLegacyBundleName));
    if (!QDir(appPath).exists()) {
        processOutput(QStringLiteral("/usr/bin/hdiutil"),
                      {QStringLiteral("detach"), mountPoint, QStringLiteral("-force")});
        if (errorMessage)
            *errorMessage = QStringLiteral("The disk image does not contain OwlSwitch.app.");
        return {};
    }
    return {mountPoint, appPath};
}

void detachDiskImage(const QString &mountPoint) {
    if (!mountPoint.isEmpty())
        processOutput(QStringLiteral("/usr/bin/hdiutil"),
                      {QStringLiteral("detach"), mountPoint, QStringLiteral("-force")});
}

} // namespace

UpdateManager::UpdateManager(const QString &dataRoot, QObject *parent)
    : UpdateManager(dataRoot, new QNetworkAccessManager, parent) {
    m_network->setParent(this);
}

UpdateManager::UpdateManager(const QString &dataRoot, QNetworkAccessManager *network,
                             QObject *parent)
    : QObject(parent), m_dataRoot(dataRoot), m_network(network) {
    Q_ASSERT(m_network);
    m_runningAppPath = runningAppBundlePath();
    const QString runningTeamIdentifier = teamIdentifierForApp(m_runningAppPath);
    m_expectedTeamIdentifier = QString::fromLatin1(kExpectedTeamIdentifier);
    const QFileInfo appInfo(m_runningAppPath);
    m_canInstall = appInfo.exists() && appInfo.fileName().endsWith(QStringLiteral(".app")) &&
                   runningTeamIdentifier == m_expectedTeamIdentifier &&
                   QFileInfo(appInfo.absolutePath()).isWritable();
}

QString UpdateManager::currentVersion() const {
    return QCoreApplication::applicationVersion();
}

bool UpdateManager::updateAvailable() const {
    return !m_latestVersion.isEmpty() && compareVersions(m_latestVersion, currentVersion()) > 0;
}

int UpdateManager::compareVersions(const QString &left, const QString &right) {
    auto parts = [](QString value) {
        value = value.trimmed();
        if (value.startsWith('v', Qt::CaseInsensitive))
            value.remove(0, 1);
        value = value.section('-', 0, 0);
        const QStringList raw = value.split('.');
        QList<int> result;
        result.reserve(raw.size());
        for (const QString &part : raw) {
            const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)")).match(part);
            result.push_back(match.hasMatch() ? match.captured(1).toInt() : 0);
        }
        return result;
    };
    const QList<int> a = parts(left);
    const QList<int> b = parts(right);
    const int count = qMax(a.size(), b.size());
    for (int index = 0; index < count; ++index) {
        const int av = index < a.size() ? a.at(index) : 0;
        const int bv = index < b.size() ? b.at(index) : 0;
        if (av != bv)
            return av < bv ? -1 : 1;
    }
    return 0;
}

void UpdateManager::setStatus(const QString &state, const QString &message) {
    m_state = state;
    m_statusMessage = message;
    emit changed();
}

void UpdateManager::resetTransfer() {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_downloadFile.isOpen())
        m_downloadFile.close();
    m_downloadHash.reset();
    m_progress = 0.0;
}

void UpdateManager::checkForUpdates() {
    startCheck(false);
}

void UpdateManager::checkForUpdatesOnLaunch() {
    startCheck(true);
}

void UpdateManager::startCheck(bool startedOnLaunch) {
    resetTransfer();
    m_checkStartedOnLaunch = startedOnLaunch;
    m_latestVersion.clear();
    m_releaseNotes.clear();
    m_assetUrl.clear();
    m_assetName.clear();
    m_expectedDigest.clear();
    m_expectedSize = -1;
    setStatus(QStringLiteral("checking"), QStringLiteral("Checking GitHub for updates..."));

    QNetworkRequest request(QUrl(QString::fromLatin1(kReleaseApi)));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OwlSwitch/%1").arg(currentVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReleaseResponse(reply); });
}

void UpdateManager::handleReleaseResponse(QNetworkReply *reply) {
    if (reply != m_reply)
        return;
    m_reply = nullptr;
    const bool startedOnLaunch = m_checkStartedOnLaunch;
    m_checkStartedOnLaunch = false;
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError) {
        setStatus(QStringLiteral("error"), QStringLiteral("Could not check for updates. Try again later."));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        setStatus(QStringLiteral("error"), QStringLiteral("GitHub returned an invalid release response."));
        return;
    }
    const QJsonObject release = document.object();
    m_latestVersion = release.value(QStringLiteral("tag_name")).toString();
    if (m_latestVersion.startsWith('v', Qt::CaseInsensitive))
        m_latestVersion.remove(0, 1);
    m_releaseNotes = release.value(QStringLiteral("body")).toString().trimmed();
    if (compareVersions(m_latestVersion, currentVersion()) <= 0) {
        setStatus(QStringLiteral("upToDate"),
                  QStringLiteral("Version %1 is up to date.").arg(currentVersion()));
        return;
    }

    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(QStringLiteral("-macOS-arm64.dmg"), Qt::CaseInsensitive))
            continue;
        m_assetName = name;
        m_assetUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        m_expectedDigest = asset.value(QStringLiteral("digest")).toString();
        if (m_expectedDigest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive))
            m_expectedDigest.remove(0, 7);
        m_expectedDigest = m_expectedDigest.toLower();
        m_expectedSize = asset.value(QStringLiteral("size")).toInteger(-1);
        break;
    }
    if (!m_assetUrl.isValid() || m_expectedDigest.size() != 64 || m_expectedSize <= 0) {
        setStatus(QStringLiteral("error"),
                  QStringLiteral("The release is missing its verified Apple Silicon disk image."));
        return;
    }
    setStatus(QStringLiteral("available"),
              QStringLiteral("Version %1 is available.").arg(m_latestVersion));
    if (startedOnLaunch)
        emit launchUpdateAvailable(m_latestVersion);
}

void UpdateManager::downloadUpdate() {
    if (!updateAvailable() || !m_assetUrl.isValid() || m_expectedDigest.size() != 64)
        return;
    resetTransfer();
    const QString updateDirectory = QDir(m_dataRoot).filePath(QStringLiteral("updates"));
    QDir().mkpath(updateDirectory);
    m_downloadedPath = QDir(updateDirectory).filePath(m_assetName);
    const QString partialPath = m_downloadedPath + QStringLiteral(".part");
    QFile::remove(partialPath);
    m_downloadFile.setFileName(partialPath);
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus(QStringLiteral("error"), QStringLiteral("Could not create the update download file."));
        return;
    }

    QNetworkRequest request(m_assetUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OwlSwitch/%1").arg(currentVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    setStatus(QStringLiteral("downloading"), QStringLiteral("Downloading version %1...").arg(m_latestVersion));
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        const QByteArray chunk = reply->readAll();
        if (m_downloadFile.write(chunk) != chunk.size())
            reply->abort();
        else
            m_downloadHash.addData(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_progress = total > 0 ? static_cast<double>(received) / static_cast<double>(total) : 0.0;
        emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleDownloadFinished(reply); });
}

void UpdateManager::handleDownloadFinished(QNetworkReply *reply) {
    if (reply != m_reply)
        return;
    m_reply = nullptr;
    const QByteArray tail = reply->readAll();
    if (!tail.isEmpty()) {
        m_downloadFile.write(tail);
        m_downloadHash.addData(tail);
    }
    m_downloadFile.close();
    const QNetworkReply::NetworkError networkError = reply->error();
    reply->deleteLater();
    const QString partialPath = m_downloadedPath + QStringLiteral(".part");
    if (networkError != QNetworkReply::NoError) {
        QFile::remove(partialPath);
        setStatus(QStringLiteral("error"), QStringLiteral("The update download failed."));
        return;
    }
    if (QFileInfo(partialPath).size() != m_expectedSize ||
        QString::fromLatin1(m_downloadHash.result().toHex()) != m_expectedDigest) {
        QFile::remove(partialPath);
        setStatus(QStringLiteral("error"), QStringLiteral("The update failed its SHA-256 integrity check."));
        return;
    }
    QFile::remove(m_downloadedPath);
    if (!QFile::rename(partialPath, m_downloadedPath)) {
        QFile::remove(partialPath);
        setStatus(QStringLiteral("error"), QStringLiteral("Could not finalize the update download."));
        return;
    }

    setStatus(QStringLiteral("verifying"), QStringLiteral("Verifying Apple signature and notarization..."));
    QString verificationError;
    if (!verifyDiskImage(m_downloadedPath, &verificationError)) {
        QFile::remove(m_downloadedPath);
        setStatus(QStringLiteral("error"), verificationError);
        return;
    }
    m_progress = 1.0;
    setStatus(QStringLiteral("ready"), m_canInstall
        ? QStringLiteral("Version %1 is verified and ready to install.").arg(m_latestVersion)
        : QStringLiteral("Version %1 is verified. Open the disk image to install it.").arg(m_latestVersion));
}

QString UpdateManager::runningAppBundlePath() const {
    QDir directory(QCoreApplication::applicationDirPath());
    if (directory.dirName() != QStringLiteral("MacOS") || !directory.cdUp() ||
        directory.dirName() != QStringLiteral("Contents") || !directory.cdUp())
        return {};
    return directory.absolutePath();
}

QString UpdateManager::teamIdentifierForApp(const QString &appPath) const {
    if (appPath.isEmpty())
        return {};
    int exitCode = -1;
    const QString output = processOutput(QStringLiteral("/usr/bin/codesign"),
        {QStringLiteral("-dv"), QStringLiteral("--verbose=4"), appPath}, &exitCode);
    if (exitCode != 0)
        return {};
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("(?:^|\\n)TeamIdentifier=([^\\r\\n]+)")).match(output);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

bool UpdateManager::verifyAppBundle(const QString &appPath, QString *errorMessage) const {
    int exitCode = -1;
    processOutput(QStringLiteral("/usr/bin/codesign"),
                  {QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict"), appPath},
                  &exitCode);
    if (exitCode != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("The update has an invalid Apple code signature.");
        return false;
    }
    processOutput(QStringLiteral("/usr/sbin/spctl"),
                  {QStringLiteral("-a"), QStringLiteral("-t"), QStringLiteral("exec"), appPath}, &exitCode);
    if (exitCode != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("macOS did not accept the update as notarized software.");
        return false;
    }
    if (m_expectedTeamIdentifier.isEmpty() ||
        teamIdentifierForApp(appPath) != m_expectedTeamIdentifier) {
        if (errorMessage) *errorMessage = QStringLiteral("The update was signed by an unexpected developer.");
        return false;
    }

    const QString infoPath = QDir(appPath).filePath(QStringLiteral("Contents/Info.plist"));
    const QString bundleId = processOutput(QStringLiteral("/usr/bin/plutil"),
        {QStringLiteral("-extract"), QStringLiteral("CFBundleIdentifier"), QStringLiteral("raw"),
         QStringLiteral("-o"), QStringLiteral("-"), infoPath}, &exitCode).trimmed();
    if (exitCode != 0 || bundleId != QString::fromLatin1(kBundleIdentifier)) {
        if (errorMessage) *errorMessage = QStringLiteral("The update has the wrong bundle identifier.");
        return false;
    }
    const QString executableName = processOutput(QStringLiteral("/usr/bin/plutil"),
        {QStringLiteral("-extract"), QStringLiteral("CFBundleExecutable"), QStringLiteral("raw"),
         QStringLiteral("-o"), QStringLiteral("-"), infoPath}, &exitCode).trimmed();
    const QString executablePath = QDir(appPath).filePath(QStringLiteral("Contents/MacOS/") + executableName);
    const QString architectures = processOutput(QStringLiteral("/usr/bin/lipo"),
        {QStringLiteral("-archs"), executablePath}, &exitCode);
    if (exitCode != 0 || !architectures.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
                               .contains(QStringLiteral("arm64"))) {
        if (errorMessage) *errorMessage = QStringLiteral("The update is not compatible with Apple Silicon.");
        return false;
    }
    const QString version = processOutput(QStringLiteral("/usr/bin/plutil"),
        {QStringLiteral("-extract"), QStringLiteral("CFBundleShortVersionString"), QStringLiteral("raw"),
         QStringLiteral("-o"), QStringLiteral("-"), infoPath}, &exitCode).trimmed();
    if (exitCode != 0 || compareVersions(version, m_latestVersion) != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("The app version does not match the GitHub release.");
        return false;
    }
    return true;
}

bool UpdateManager::verifyDiskImage(const QString &path, QString *errorMessage) const {
    if (!QFileInfo(path).isFile()) {
        if (errorMessage) *errorMessage = QStringLiteral("The downloaded update no longer exists.");
        return false;
    }
    int exitCode = -1;
    processOutput(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("--verify"), path}, &exitCode);
    if (exitCode != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("The update disk image has an invalid signature.");
        return false;
    }
    processOutput(QStringLiteral("/usr/sbin/spctl"),
                  {QStringLiteral("-a"), QStringLiteral("-t"), QStringLiteral("open"),
                   QStringLiteral("--context"), QStringLiteral("context:primary-signature"), path},
                  &exitCode);
    if (exitCode != 0) {
        if (errorMessage) *errorMessage = QStringLiteral("macOS did not accept the update disk image as notarized software.");
        return false;
    }
    const MountedImage mounted = mountDiskImage(path, errorMessage);
    if (mounted.mountPoint.isEmpty())
        return false;
    const bool valid = verifyAppBundle(mounted.appPath, errorMessage);
    detachDiskImage(mounted.mountPoint);
    return valid;
}

void UpdateManager::cancel() {
    const QString partialPath = m_downloadedPath.isEmpty()
        ? QString() : m_downloadedPath + QStringLiteral(".part");
    resetTransfer();
    m_checkStartedOnLaunch = false;
    if (!partialPath.isEmpty())
        QFile::remove(partialPath);
    setStatus(updateAvailable() ? QStringLiteral("available") : QStringLiteral("idle"),
              updateAvailable() ? QStringLiteral("Update download cancelled.")
                                : QStringLiteral("Update check cancelled."));
}

void UpdateManager::openDownloadedUpdate() {
    if (m_state == QStringLiteral("ready") && QFileInfo(m_downloadedPath).isFile())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_downloadedPath));
}

void UpdateManager::installAndRestart() {
    if (m_state != QStringLiteral("ready") || !m_canInstall || !QFileInfo(m_downloadedPath).isFile())
        return;

    const QString helperPath = QDir(m_dataRoot).filePath(QStringLiteral("update-helper.zsh"));
    const QString logPath = QDir(m_dataRoot).filePath(QStringLiteral("update-helper.log"));
    QFile helper(helperPath);
    if (!helper.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setStatus(QStringLiteral("error"), QStringLiteral("Could not create the update installer."));
        return;
    }
    QTextStream stream(&helper);
    stream << "#!/bin/zsh\n"
              "set -eu\n"
              "pid=$1\n"
              "dmg=$2\n"
              "target=$3\n"
              "team=$4\n"
              "bundle_id=$5\n"
              "expected_digest=$6\n"
              "expected_size=$7\n"
              "expected_version=$8\n"
              "log=$9\n"
              "exec >>\"$log\" 2>&1\n"
              "while /bin/kill -0 \"$pid\" 2>/dev/null; do /bin/sleep 0.2; done\n"
              "mount_point=''\n"
              "new_app=\"${target}.update-new\"\n"
              "old_app=\"${target}.update-old\"\n"
              "cleanup() {\n"
              "  if [[ -n \"$mount_point\" ]]; then /usr/bin/hdiutil detach \"$mount_point\" -force >/dev/null 2>&1 || true; fi\n"
              "  /bin/rm -rf \"$new_app\"\n"
              "}\n"
              "trap cleanup EXIT\n"
              "actual_digest=$(/usr/bin/shasum -a 256 \"$dmg\" | /usr/bin/awk '{ print $1 }')\n"
              "actual_size=$(/usr/bin/stat -f '%z' \"$dmg\")\n"
              "[[ \"$actual_digest\" == \"$expected_digest\" && \"$actual_size\" == \"$expected_size\" ]]\n"
              "/usr/bin/codesign --verify \"$dmg\"\n"
              "/usr/sbin/spctl -a -t open --context context:primary-signature \"$dmg\"\n"
              "attach_output=$(/usr/bin/hdiutil attach -nobrowse -readonly -noautoopen \"$dmg\")\n"
              "mount_point=$(print -r -- \"$attach_output\" | /usr/bin/awk -F '\\t' '/\\/Volumes\\// { value=$NF } END { print value }')\n";
    stream << "source_app=\"$mount_point/" << QString::fromLatin1(kPrimaryBundleName) << "\"\n"
              "if [[ ! -d \"$source_app\" ]]; then source_app=\"$mount_point/"
           << QString::fromLatin1(kLegacyBundleName) << "\"; fi\n"
              "[[ -d \"$source_app\" ]]\n"
              "/usr/bin/codesign --verify --deep --strict \"$source_app\"\n"
              "/usr/sbin/spctl -a -t exec \"$source_app\"\n"
              "actual_team=$(/usr/bin/codesign -dv --verbose=4 \"$source_app\" 2>&1 | /usr/bin/sed -n 's/^TeamIdentifier=//p')\n"
              "[[ \"$actual_team\" == \"$team\" ]]\n"
              "actual_id=$(/usr/bin/plutil -extract CFBundleIdentifier raw -o - \"$source_app/Contents/Info.plist\")\n"
              "[[ \"$actual_id\" == \"$bundle_id\" ]]\n"
              "actual_version=$(/usr/bin/plutil -extract CFBundleShortVersionString raw -o - \"$source_app/Contents/Info.plist\")\n"
              "canonical_version() { local value=${1#v}; while [[ \"$value\" == *'.0' ]]; do value=${value%.0}; done; print -r -- \"$value\"; }\n"
              "[[ \"$(canonical_version \"$actual_version\")\" == \"$(canonical_version \"$expected_version\")\" ]]\n"
              "source_executable=$(/usr/bin/plutil -extract CFBundleExecutable raw -o - \"$source_app/Contents/Info.plist\")\n"
              "[[ -n \"$source_executable\" ]]\n"
              "/usr/bin/lipo -archs \"$source_app/Contents/MacOS/$source_executable\" | /usr/bin/grep -qw arm64\n"
              "/bin/rm -rf \"$new_app\" \"$old_app\"\n"
              "/usr/bin/ditto \"$source_app\" \"$new_app\"\n"
              "/usr/bin/codesign --verify --deep --strict \"$new_app\"\n"
              "/usr/sbin/spctl -a -t exec \"$new_app\"\n"
              "/bin/mv \"$target\" \"$old_app\"\n"
              "if ! /bin/mv \"$new_app\" \"$target\"; then /bin/mv \"$old_app\" \"$target\"; exit 1; fi\n"
              "/bin/rm -rf \"$old_app\"\n"
              "/usr/bin/open \"$target\"\n"
              "trap - EXIT\n"
              "cleanup\n"
              "/bin/rm -f -- \"$0\"\n";
    helper.close();
    helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QStringList arguments = {
        helperPath,
        QString::number(QCoreApplication::applicationPid()),
        m_downloadedPath,
        m_runningAppPath,
        m_expectedTeamIdentifier,
        QString::fromLatin1(kBundleIdentifier),
        m_expectedDigest,
        QString::number(m_expectedSize),
        m_latestVersion,
        logPath
    };
    if (!QProcess::startDetached(QStringLiteral("/bin/zsh"), arguments)) {
        setStatus(QStringLiteral("error"), QStringLiteral("Could not start the update installer."));
        return;
    }
    setStatus(QStringLiteral("installing"), QStringLiteral("Installing update and restarting..."));
    QCoreApplication::quit();
}
