#include "AppDataMigration.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class AppDataMigrationTest final : public QObject {
    Q_OBJECT

private slots:
    void createsPreferredDirectory();
    void movesLegacyDirectoryWithoutLosingData();
    void leavesExistingPreferredDirectoryUntouched();
};

void AppDataMigrationTest::createsPreferredDirectory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString preferred = root.filePath(QStringLiteral("owl-switch"));
    const QString legacy = root.filePath(QStringLiteral("legacy"));

    QCOMPARE(AppDataMigration::prepareDataRoot(preferred, legacy),
             QDir(preferred).canonicalPath());
    QVERIFY(QFileInfo(preferred).isDir());
}

void AppDataMigrationTest::movesLegacyDirectoryWithoutLosingData()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString preferred = root.filePath(QStringLiteral("owl-switch"));
    const QString legacy = root.filePath(QStringLiteral("legacy"));
    QVERIFY(QDir().mkpath(legacy));
    QFile config(QDir(legacy).filePath(QStringLiteral("config.json")));
    QVERIFY(config.open(QIODevice::WriteOnly));
    QCOMPARE(config.write("preserved\n"), qint64(10));
    config.close();

    QCOMPARE(AppDataMigration::prepareDataRoot(preferred, legacy),
             QDir(preferred).canonicalPath());
    QVERIFY(!QFileInfo::exists(legacy));
    QFile migrated(QDir(preferred).filePath(QStringLiteral("config.json")));
    QVERIFY(migrated.open(QIODevice::ReadOnly));
    QCOMPARE(migrated.readAll(), QByteArrayLiteral("preserved\n"));
}

void AppDataMigrationTest::leavesExistingPreferredDirectoryUntouched()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString preferred = root.filePath(QStringLiteral("owl-switch"));
    const QString legacy = root.filePath(QStringLiteral("legacy"));
    QVERIFY(QDir().mkpath(preferred));
    QVERIFY(QDir().mkpath(legacy));

    QCOMPARE(AppDataMigration::prepareDataRoot(preferred, legacy),
             QDir(preferred).canonicalPath());
    QVERIFY(QFileInfo(legacy).isDir());
}

QTEST_MAIN(AppDataMigrationTest)
#include "AppDataMigrationTest.moc"
