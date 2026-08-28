#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>

class TrackPreviewPolicyTest final : public QObject {
    Q_OBJECT

private slots:
    void openingAndClosingWindows();
    void overlappingShortVideoStaysActive();
};

static QObject *createPolicy(QQmlEngine &engine, QQmlComponent &component)
{
    engine.addImportPath(QStringLiteral(TEST_SOURCE_ROOT "/views"));
    component.setData(R"(
        import QtQml
        import Components
        QtObject {
            function active(positionMs, durationMs, windowMs) {
                return TrackPreviewPolicy.isActive(positionMs, durationMs, windowMs)
            }
        }
    )", QUrl());
    return component.create();
}

static bool isActive(QObject *policy, int positionMs, int durationMs, int windowMs)
{
    QVariant result;
    const bool invoked = QMetaObject::invokeMethod(
        policy, "active", Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, positionMs), Q_ARG(QVariant, durationMs),
        Q_ARG(QVariant, windowMs));
    return invoked && result.toBool();
}

void TrackPreviewPolicyTest::openingAndClosingWindows()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> policy(createPolicy(engine, component));
    QVERIFY2(policy, qPrintable(component.errorString()));

    QVERIFY(isActive(policy.data(), 0, 120000, 15000));
    QVERIFY(isActive(policy.data(), 15000, 120000, 15000));
    QVERIFY(!isActive(policy.data(), 15001, 120000, 15000));
    QVERIFY(!isActive(policy.data(), 104999, 120000, 15000));
    QVERIFY(isActive(policy.data(), 105000, 120000, 15000));
    QVERIFY(isActive(policy.data(), 119999, 120000, 15000));
}

void TrackPreviewPolicyTest::overlappingShortVideoStaysActive()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    QScopedPointer<QObject> policy(createPolicy(engine, component));
    QVERIFY2(policy, qPrintable(component.errorString()));

    for (int position = 0; position <= 25000; position += 1000)
        QVERIFY(isActive(policy.data(), position, 25000, 15000));
    QVERIFY(!isActive(policy.data(), -1, 25000, 15000));
    QVERIFY(!isActive(policy.data(), 20000, 25000, 0));
}

QTEST_GUILESS_MAIN(TrackPreviewPolicyTest)
#include "TrackPreviewPolicyTest.moc"
