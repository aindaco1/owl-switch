#include "AppCore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class BackendWithoutAuth final : public QObject {
    Q_OBJECT
};

class AppCoreTest final : public QObject {
    Q_OBJECT

private slots:
    void dottedSettingsRoundTrip();
    void listSettingsRoundTrip();
    void qmlListSettingsRoundTrip();
    void nonAuthBackendReturnsNoAuthState();
    void discoversReleaseModulesInExpectedOrder();
    void migratesLegacyModuleSettings();
};

void AppCoreTest::dottedSettingsRoundTrip()
{
    QTemporaryDir root;
    QTemporaryDir data;
    QVERIFY(root.isValid());
    QVERIFY(data.isValid());

    AppCore core(root.path(), data.path());
    core.save_setting(QStringLiteral("com.example.module"),
                      QStringLiteral("remote_keymap.select"),
                      QStringLiteral("Return"));

    QCOMPARE(core.get_setting(QStringLiteral("com.example.module"),
                              QStringLiteral("remote_keymap.select")).toString(),
             QStringLiteral("Return"));
}

void AppCoreTest::qmlListSettingsRoundTrip()
{
    QTemporaryDir root;
    QTemporaryDir data;
    QVERIFY(root.isValid());
    QVERIFY(data.isValid());

    AppCore core(root.path(), data.path());
    QJSEngine engine;
    const QJSValue favorites = engine.evaluate(
        QStringLiteral("['https://one.tumblr.com/', 'https://two.tumblr.com/']"));
    core.save_setting(QStringLiteral("com.owlswitch.tumblr_screensaver"),
                      QStringLiteral("favorites"), QVariant::fromValue(favorites));

    QCOMPARE(core.get_setting(QStringLiteral("com.owlswitch.tumblr_screensaver"),
                              QStringLiteral("favorites")).toStringList(),
             QStringList({QStringLiteral("https://one.tumblr.com/"),
                          QStringLiteral("https://two.tumblr.com/")}));
}

void AppCoreTest::listSettingsRoundTrip()
{
    QTemporaryDir root;
    QTemporaryDir data;
    QVERIFY(root.isValid());
    QVERIFY(data.isValid());

    AppCore core(root.path(), data.path());
    const QStringList favorites = {
        QStringLiteral("https://example-one.tumblr.com/"),
        QStringLiteral("https://example-two.tumblr.com/")
    };
    core.save_setting(QStringLiteral("com.owlswitch.tumblr_screensaver"),
                      QStringLiteral("favorites"), favorites);

    QCOMPARE(core.get_setting(QStringLiteral("com.owlswitch.tumblr_screensaver"),
                              QStringLiteral("favorites")).toStringList(),
             favorites);
}

void AppCoreTest::nonAuthBackendReturnsNoAuthState()
{
    QTemporaryDir root;
    QTemporaryDir data;
    QVERIFY(root.isValid());
    QVERIFY(data.isValid());

    AppCore core(root.path(), data.path());
    BackendWithoutAuth backend;
    core.registerModule(QStringLiteral("com.example.module"), QString(), &backend, nullptr);
    QCOMPARE(core.get_module_auth_state(QStringLiteral("com.example.module")), QString());
}

void AppCoreTest::discoversReleaseModulesInExpectedOrder()
{
    QTemporaryDir data;
    QVERIFY(data.isValid());
    AppCore core(QStringLiteral(TEST_SOURCE_ROOT), data.path());
    QSignalSpy modulesSpy(&core, &AppCore::modulesLoaded);

    core.scan_for_modules();
    QCOMPARE(modulesSpy.size(), 1);
    const QVariantList modules = modulesSpy.constFirst().constFirst().toList();
    QStringList names;
    for (const QVariant &module : modules)
        names.append(module.toMap().value(QStringLiteral("name")).toString());
    QCOMPARE(names, QStringList({QStringLiteral("Jellyfin"), QStringLiteral("Karaoke"),
                                 QStringLiteral("Retro"), QStringLiteral("Tumblr"),
                                 QStringLiteral("Nature"), QStringLiteral("Local")}));

    const QVariantMap nature = core.get_module_info(QStringLiteral("com.owlswitch.nature")).toMap();
    QCOMPARE(nature.value(QStringLiteral("name")).toString(), QStringLiteral("Nature"));
    QVERIFY(nature.value(QStringLiteral("icon")).toUrl().isValid());
}

void AppCoreTest::migratesLegacyModuleSettings()
{
    QTemporaryDir root;
    QTemporaryDir data;
    QVERIFY(root.isValid());
    QVERIFY(data.isValid());

    const QJsonObject legacyConfig{
        {QStringLiteral("app"), QJsonObject{
            {QStringLiteral("startup_module"), QStringLiteral("com.240mp.karaoke")}}},
        {QStringLiteral("modules"), QJsonObject{
            {QStringLiteral("com.240mp.karaoke"), QJsonObject{
                {QStringLiteral("enabled"), true}}},
            {QStringLiteral("com.240mp.local_files"), QJsonObject{
                {QStringLiteral("media_directory"), QStringLiteral("/tmp/media")}}}}}
    };
    QFile config(data.filePath(QStringLiteral("config.json")));
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write(QJsonDocument(legacyConfig).toJson());
    config.close();

    AppCore core(root.path(), data.path());
    QCOMPARE(core.get_setting(QStringLiteral("com.owlswitch.local_files"),
                              QStringLiteral("media_directory")).toString(),
             QStringLiteral("/tmp/media"));
    QFile migratedFile(data.filePath(QStringLiteral("config.json")));
    QVERIFY(migratedFile.open(QIODevice::ReadOnly));
    const QJsonObject migrated =
        QJsonDocument::fromJson(migratedFile.readAll()).object();
    QCOMPARE(migrated.value(QStringLiteral("app")).toObject()
                 .value(QStringLiteral("startup_module")).toString(),
             QStringLiteral("com.owlswitch.karaoke"));
    const QJsonObject modules = migrated.value(QStringLiteral("modules")).toObject();
    QVERIFY(modules.contains(QStringLiteral("com.owlswitch.karaoke")));
    QVERIFY(modules.contains(QStringLiteral("com.owlswitch.local_files")));
    QVERIFY(!modules.contains(QStringLiteral("com.240mp.karaoke")));
    QVERIFY(!modules.contains(QStringLiteral("com.240mp.local_files")));
}

QTEST_MAIN(AppCoreTest)
#include "AppCoreTest.moc"
