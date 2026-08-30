#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <array>

#include "RightShiftBackTracker.h"

class AppCore;
class QQuickWindow;

class InputManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool gamepadConnected READ gamepadConnected NOTIFY gamepadConnectedChanged)
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY hintsChanged)
    Q_PROPERTY(bool remapCaptureActive READ remapCaptureActive NOTIFY remapCaptureActiveChanged)
    Q_PROPERTY(QVariantList remappableActions READ remappableActions NOTIFY remappingsChanged)

public:
    explicit InputManager(AppCore *appCore = nullptr, QObject *parent = nullptr);
    ~InputManager() override;
    void setTargetWindow(QQuickWindow *window);
    bool gamepadConnected() const { return m_gamepadCount > 0; }
    QVariantMap hints() const;
    bool remapCaptureActive() const { return m_remapCaptureActive; }
    QVariantList remappableActions() const;

    Q_INVOKABLE QString keyDisplayName(int extendedKeyId) const;
    Q_INVOKABLE void startRemapCapture(const QString &actionId);
    Q_INVOKABLE void cancelRemapCapture();
    Q_INVOKABLE void resetRemappings();

signals:
    void gamepadConnectedChanged();
    void hintsChanged();
    void mpvKeyRequested(const QString &key);
    void remapCaptureActiveChanged();
    void remappingsChanged();
    void remapCaptureRejected(const QString &reason);
    void remapCaptured(const QString &actionId, const QString &inputName);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    enum class Action { None, Up, Down, Left, Right, Select, Back, PlayPause };
    struct ActionDescriptor {
        Action action;
        const char *id;
        const char *label;
        int qtKey;
        const char *mpvKey;
        bool remappable;
    };

    friend void inputManagerControllerCountChanged(InputManager *, int);
    friend void inputManagerDeliverKey(InputManager *, int, const QString &);
    friend void inputManagerDeliverAction(InputManager *, const QString &);
    static const std::array<ActionDescriptor, 7> &actionDescriptors();
    static Action actionFromId(const QString &actionId);
    static int qtKeyForAction(Action action);
    static QString mpvKeyForAction(Action action);
    static bool isValidInputId(int key);
    static bool isReservedDefaultKey(int key);
    int customKeyForAction(Action action) const;
    void completeRemapCapture(int extendedKeyId);
    void loadRemappings();
    void onAppSettingChanged(const QString &key, const QString &value);
    void setGamepadCount(int count);
    void deliverAction(Action action);
    void deliverKey(int qtKey, const QString &mpvKey);

    AppCore *m_appCore = nullptr;
    QPointer<QQuickWindow> m_window;
    int m_gamepadCount = 0;
    QHash<int, Action> m_keyRemap;
    Action m_captureAction = Action::None;
    bool m_remapCaptureActive = false;
    RightShiftBackTracker m_rightShiftBackTracker;
    void *m_connectObserver = nullptr;
    void *m_disconnectObserver = nullptr;
};
