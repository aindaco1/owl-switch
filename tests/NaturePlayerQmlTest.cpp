#include <QtTest>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>

class SoundFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sourceUrl READ sourceUrl CONSTANT)
public:
    bool wanted = false;
    bool paused = false;
    int starts = 0;
    int prepares = 0;
    QString sourceUrl() const { return {}; }
    Q_INVOKABLE void prepare() { ++prepares; }
    Q_INVOKABLE void start() { if (!wanted) { wanted = true; ++starts; } }
    Q_INVOKABLE void stop() { wanted = false; }
    Q_INVOKABLE void setPaused(bool value) { paused = value; }
signals:
    void statusChanged(const QString &message);
    void inputKey(const QString &key);
};

class NatureFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *soundtrack READ soundtrack CONSTANT)
public:
    SoundFixture sound;
    QObject *soundtrack() { return &sound; }
    Q_INVOKABLE void loadLatestObservations() {}
    Q_INVOKABLE void refreshObservations() { emit refreshStarted(true); }
signals:
    void refreshStarted(bool cached);
    void observationsLoaded(const QVariantList &items, bool cached, bool stale);
    void loadFailed(const QString &message, bool cached);
};

class NaturePlayerQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void sessionLifecycle_data();
    void sessionLifecycle();
};

void NaturePlayerQmlTest::sessionLifecycle_data()
{
    QTest::addColumn<bool>("external");
    QTest::newRow("controller-output") << false;
    QTest::newRow("media-output") << true;
}

void NaturePlayerQmlTest::sessionLifecycle()
{
    QFETCH(bool, external);
    QTemporaryDir data;
    QImage photo(1280, 720, QImage::Format_RGB32);
    photo.fill(QColor("#174329"));
    const QString path = data.filePath("photo.png");
    QVERIFY(photo.save(path));
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(TEST_SOURCE_ROOT "/views"));
    NatureFixture backend;
    engine.rootContext()->setContextProperty("natureBackend", &backend);
    QQmlComponent hostComponent(&engine);
    hostComponent.setData(R"qml(import QtQuick
Item {
    width: 1280; height: 720
    property real sh: height
    property real sw: width
    property color surfaceColor: "black"
    property color primaryColor: "white"
    property color secondaryColor: "white"
    property color tertiaryColor: "gray"
    property string globalFont: "Helvetica"
    property bool hasMediaOutputScreen: false
    property alias mediaOutputLayer: output
    property int leases: 0
    signal mediaOutputKeyPressed(var event)
    function openMediaOutput(opaque, focus) { ++leases; return true }
    function closeMediaOutput() { --leases }
    Item { id: output; width: 1280; height: 720 }
})qml", QUrl());
    QScopedPointer<QObject> host(hostComponent.create());
    QVERIFY2(host, qPrintable(hostComponent.errorString()));
    host->setProperty("hasMediaOutputScreen", external);
    engine.rootContext()->setContextProperty("root", host.data());
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QStringLiteral(TEST_SOURCE_ROOT "/modules/nature/views/Player.qml")));
    QScopedPointer<QObject> player(component.create());
    QVERIFY2(player, qPrintable(component.errorString()));
    auto item = qobject_cast<QQuickItem *>(player.data());
    QVERIFY(item);
    QQuickWindow window;
    window.resize(1280, 720);
    qobject_cast<QQuickItem *>(host.data())->setParentItem(window.contentItem());
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(1280, 720));
    window.show();
    item->forceActiveFocus();
    QCOMPARE(backend.sound.prepares, 1);
    QVERIFY(!backend.sound.wanted);
    const QVariantList observations{QVariantMap{
        {"url", QUrl::fromLocalFile(path).toString()}, {"title", "FOREST OBSERVATION"},
        {"scientificName", "Example species"}, {"city", "Boulder"},
        {"region", "Colorado"}, {"country", "United States"}}};
    emit backend.observationsLoaded(observations, true, false);
    QTRY_VERIFY_WITH_TIMEOUT(backend.sound.wanted, 2000);
    QCOMPARE(backend.sound.starts, 1);
    QCOMPARE(host->property("leases").toInt(), external ? 1 : 0);
    emit backend.sound.inputKey("SPACE");
    QVERIFY(player->property("paused").toBool());
    QVERIFY(backend.sound.paused);
    emit backend.observationsLoaded(observations, false, false);
    QTest::qWait(100);
    QVERIFY(backend.sound.paused);
    QCOMPARE(backend.sound.starts, 1);
    emit backend.sound.inputKey("ENTER");
    QVERIFY(!backend.sound.paused);
    emit backend.sound.inputKey("RIGHT");
    QCOMPARE(backend.sound.starts, 1);
    emit backend.sound.statusChanged("NATURE SOUND UNAVAILABLE / SLIDESHOW CONTINUES");
    QCOMPARE(player->property("loadState").toString(), "playing");
    if (!external && qEnvironmentVariableIsSet("NATURE_QML_SCREENSHOT")) {
        QTest::qWait(150);
        QVERIFY(window.grabWindow().save(qEnvironmentVariable("NATURE_QML_SCREENSHOT")));
    }
    QSignalSpy back(player.data(), SIGNAL(goBack()));
    emit backend.sound.inputKey("ESC");
    QCOMPARE(back.count(), 1);
    QVERIFY(!backend.sound.wanted);
    player.reset();
    QCOMPARE(host->property("leases").toInt(), 0);
}

QTEST_MAIN(NaturePlayerQmlTest)
#include "NaturePlayerQmlTest.moc"
