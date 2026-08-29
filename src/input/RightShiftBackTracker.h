#pragma once

class RightShiftBackTracker final {
public:
    void press(bool autoRepeat)
    {
        if (autoRepeat || m_pressed)
            return;
        m_pressed = true;
        m_usedAsModifier = false;
    }

    void useAsModifier()
    {
        if (m_pressed)
            m_usedAsModifier = true;
    }

    bool release(bool autoRepeat)
    {
        if (autoRepeat || !m_pressed)
            return false;
        const bool shouldGoBack = !m_usedAsModifier;
        m_pressed = false;
        m_usedAsModifier = false;
        return shouldGoBack;
    }

    bool pressed() const { return m_pressed; }

private:
    bool m_pressed = false;
    bool m_usedAsModifier = false;
};
