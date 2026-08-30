#include "input/InputManager.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

class RemapControlsQmlTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsCanonicalActionsAndCaptureOverlay();
};

void RemapControlsQmlTest::loadsCanonicalActionsAndCaptureOverlay()
{
    QObject rootContext;
    rootContext.setProperty("sw", 640.0);
    rootContext.setProperty("sh", 480.0);
    rootContext.setProperty("primaryColor", QStringLiteral("#ffffff"));
    rootContext.setProperty("secondaryColor", QStringLiteral("#aaaaaa"));
    rootContext.setProperty("tertiaryColor", QStringLiteral("#777777"));
    rootContext.setProperty("surfaceColor", QStringLiteral("#000000"));
    rootContext.setProperty("accentColor", QStringLiteral("#00ffff"));
    rootContext.setProperty("globalFont", QStringLiteral("Monaco"));
    rootContext.setProperty("hints", QVariantMap{
        {QStringLiteral("back"), QStringLiteral("ESC")},
        {QStringLiteral("navigate"), QStringLiteral("ARROWS")},
        {QStringLiteral("select"), QStringLiteral("ENTER")},
    });

    InputManager inputManager;
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(TEST_SOURCE_ROOT "/views"));
    engine.rootContext()->setContextProperty(QStringLiteral("root"), &rootContext);
    engine.rootContext()->setContextProperty(QStringLiteral("inputManager"), &inputManager);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(
        TEST_SOURCE_ROOT "/views/RemapControls.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    auto *pageItem = qobject_cast<QQuickItem *>(page.data());
    QVERIFY(pageItem);
    QCOMPARE(page->property("rows").toList().size(), 7);

    auto *list = page->findChild<QQuickItem *>(QStringLiteral("controlsList"));
    auto *overlay = page->findChild<QQuickItem *>(QStringLiteral("captureOverlay"));
    QVERIFY(list);
    QVERIFY(overlay);
    QVERIFY(!overlay->isVisible());

    inputManager.startRemapCapture(QStringLiteral("up"));
    QTRY_VERIFY(overlay->isVisible());
    inputManager.cancelRemapCapture();
    QTRY_VERIFY(!overlay->isVisible());
}

QTEST_MAIN(RemapControlsQmlTest)
#include "RemapControlsQmlTest.moc"
