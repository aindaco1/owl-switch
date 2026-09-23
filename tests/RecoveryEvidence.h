#pragma once

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

// Test-only export. Ordinary CTest runs do not write evidence. The development
// runner supplies a new, isolated directory and validates all rows before Jev.
inline bool writeRecoveryEvidence(const QString &id, const QString &candidate,
                                  const QJsonObject &facts)
{
    const QString directory = qEnvironmentVariable("OWLSWITCH_TEST_EVIDENCE");
    if (directory.isEmpty()) return true;
    QSaveFile file(QDir(directory).filePath(id + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {"id", id}, {"candidate", candidate}, {"facts", facts}
    }).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}
