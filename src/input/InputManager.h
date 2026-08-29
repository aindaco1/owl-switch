#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include "RightShiftBackTracker.h"

class QQuickWindow;

class InputManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool gamepadConnected READ gamepadConnected NOTIFY gamepadConnectedChanged)
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY hintsChanged)

public:
    explicit InputManager(QObject *parent = nullptr);
    ~InputManager() override;
    void setTargetWindow(QQuickWindow *window);
    bool gamepadConnected() const { return m_gamepadCount > 0; }
    QVariantMap hints() const;

signals:
    void gamepadConnectedChanged();
    void hintsChanged();
    void mpvKeyRequested(const QString &key);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    friend void inputManagerControllerCountChanged(InputManager *, int);
    friend void inputManagerDeliverKey(InputManager *, int, const QString &);
    void setGamepadCount(int count);
    void deliverKey(int qtKey, const QString &mpvKey);

    QPointer<QQuickWindow> m_window;
    int m_gamepadCount = 0;
    RightShiftBackTracker m_rightShiftBackTracker;
    void *m_connectObserver = nullptr;
    void *m_disconnectObserver = nullptr;
};
