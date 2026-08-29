#include "AppDataMigration.h"

#include <QDir>
#include <QFileInfo>

namespace {

QString normalizedDirectoryPath(const QString &path)
{
    const QString canonical = QDir(path).canonicalPath();
    return canonical.isEmpty() ? QDir(path).absolutePath() : canonical;
}

}

namespace AppDataMigration {

QString prepareDataRoot(const QString &preferredRoot, const QString &legacyRoot,
                        QString *warningMessage)
{
    if (warningMessage)
        warningMessage->clear();

    const QFileInfo preferredInfo(preferredRoot);
    const QFileInfo legacyInfo(legacyRoot);
    if (!preferredInfo.isAbsolute() || !legacyInfo.isAbsolute() ||
        preferredRoot == legacyRoot) {
        if (warningMessage)
            *warningMessage = QStringLiteral("App data paths are invalid.");
        return {};
    }

    if (preferredInfo.exists()) {
        if (!preferredInfo.isDir()) {
            if (warningMessage)
                *warningMessage = QStringLiteral("The OwlSwitch data path is not a directory.");
            return {};
        }
        return normalizedDirectoryPath(preferredRoot);
    }

    if (legacyInfo.exists() && legacyInfo.isDir()) {
        if (!QDir().mkpath(QFileInfo(preferredRoot).absolutePath()) ||
            !QDir().rename(legacyRoot, preferredRoot)) {
            if (warningMessage) {
                *warningMessage = QStringLiteral(
                    "Could not move the legacy app data directory; continuing to use it.");
            }
            return normalizedDirectoryPath(legacyRoot);
        }
        return normalizedDirectoryPath(preferredRoot);
    }

    if (!QDir().mkpath(preferredRoot)) {
        if (warningMessage)
            *warningMessage = QStringLiteral("Could not create the OwlSwitch data directory.");
        return {};
    }
    return normalizedDirectoryPath(preferredRoot);
}

}
