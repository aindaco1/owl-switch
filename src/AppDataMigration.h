#pragma once

#include <QString>

namespace AppDataMigration {

QString prepareDataRoot(const QString &preferredRoot, const QString &legacyRoot,
                        QString *warningMessage = nullptr);

}
