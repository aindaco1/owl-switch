pragma Singleton
import QtQml

QtObject {
    function isActive(positionMs, durationMs, windowMs) {
        if (positionMs < 0 || windowMs <= 0)
            return false
        if (positionMs <= windowMs)
            return true
        return durationMs > 0 && durationMs - positionMs <= windowMs
    }
}
