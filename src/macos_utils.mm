#import <AppKit/AppKit.h>
#import <IOKit/pwr_mgt/IOPMLib.h>
#import <IOKit/ps/IOPowerSources.h>
#import <IOKit/ps/IOPSKeys.h>
#include <math.h>

static IOPMAssertionID sleepAssertion = kIOPMNullAssertionID;
static CFRunLoopSourceRef powerSourceRunLoopSource = nullptr;
static bool sleepPreventionEnabled = true;
static int sleepPreventionLowBatteryThreshold = 10;

struct InternalBatteryState {
    bool found = false;
    bool onBattery = false;
    int percent = -1;
};

static void refreshMacSleepPrevention();

static void releaseSleepAssertion() {
    if (sleepAssertion == kIOPMNullAssertionID)
        return;

    IOPMAssertionRelease(sleepAssertion);
    sleepAssertion = kIOPMNullAssertionID;
    NSLog(@"[OwlSwitch] sleep prevention released");
}

static void acquireSleepAssertion() {
    if (sleepAssertion != kIOPMNullAssertionID)
        return;

    IOReturn result = IOPMAssertionCreateWithName(
        kIOPMAssertionTypePreventUserIdleSystemSleep,
        kIOPMAssertionLevelOn,
        CFSTR("OwlSwitch is running"),
        &sleepAssertion);
    if (result != kIOReturnSuccess) {
        sleepAssertion = kIOPMNullAssertionID;
        NSLog(@"[OwlSwitch] sleep prevention assertion failed: 0x%x", result);
        return;
    }
    NSLog(@"[OwlSwitch] sleep prevention active");
}

static InternalBatteryState readInternalBatteryState() {
    InternalBatteryState result;

    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info)
        return result;

    CFArrayRef sources = IOPSCopyPowerSourcesList(info);
    if (!sources) {
        CFRelease(info);
        return result;
    }

    const CFIndex count = CFArrayGetCount(sources);
    for (CFIndex i = 0; i < count; ++i) {
        CFTypeRef source = CFArrayGetValueAtIndex(sources, i);
        NSDictionary *description = (__bridge NSDictionary *)IOPSGetPowerSourceDescription(info, source);
        if (!description)
            continue;

        NSString *type = description[@kIOPSTypeKey];
        NSString *transport = description[@kIOPSTransportTypeKey];
        const bool isInternalBattery =
            [type isEqualToString:@kIOPSInternalBatteryType] ||
            [transport isEqualToString:@kIOPSInternalType];
        if (!isInternalBattery)
            continue;

        NSNumber *current = description[@kIOPSCurrentCapacityKey];
        NSNumber *maximum = description[@kIOPSMaxCapacityKey];
        NSString *powerState = description[@kIOPSPowerSourceStateKey];
        const int maxValue = maximum ? maximum.intValue : 0;

        result.found = true;
        result.onBattery = [powerState isEqualToString:@kIOPSBatteryPowerValue];
        if (current && maxValue > 0)
            result.percent = (int)lround((current.doubleValue * 100.0) / maximum.doubleValue);
        break;
    }

    CFRelease(sources);
    CFRelease(info);
    return result;
}

static bool lowBatteryAllowsSleep() {
    if (sleepPreventionLowBatteryThreshold < 0)
        return false;

    const InternalBatteryState battery = readInternalBatteryState();
    return battery.found &&
           battery.onBattery &&
           battery.percent >= 0 &&
           battery.percent <= sleepPreventionLowBatteryThreshold;
}

static void powerSourceChanged(void *) {
    refreshMacSleepPrevention();
}

static void ensurePowerSourceNotifications() {
    if (powerSourceRunLoopSource)
        return;

    powerSourceRunLoopSource = IOPSNotificationCreateRunLoopSource(powerSourceChanged, nullptr);
    if (!powerSourceRunLoopSource) {
        NSLog(@"[OwlSwitch] could not create power source notification");
        return;
    }
    CFRunLoopAddSource(CFRunLoopGetMain(), powerSourceRunLoopSource, kCFRunLoopDefaultMode);
}

static void refreshMacSleepPrevention() {
    if (!sleepPreventionEnabled || lowBatteryAllowsSleep()) {
        releaseSleepAssertion();
        return;
    }

    acquireSleepAssertion();
}

void hideMacOSMenuBar() {
    [NSApp setPresentationOptions:
        NSApplicationPresentationHideMenuBar |
        NSApplicationPresentationHideDock];
}

void configureMacSleepPrevention(bool enabled, int lowBatteryThresholdPercent) {
    sleepPreventionEnabled = enabled;
    sleepPreventionLowBatteryThreshold = lowBatteryThresholdPercent;
    ensurePowerSourceNotifications();
    refreshMacSleepPrevention();
}

void stopMacSleepPrevention() {
    releaseSleepAssertion();
    if (powerSourceRunLoopSource) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), powerSourceRunLoopSource, kCFRunLoopDefaultMode);
        CFRelease(powerSourceRunLoopSource);
        powerSourceRunLoopSource = nullptr;
    }
}

// Forces the Qt window's NSWindow to exactly cover a chosen screen.
// Called after the QML engine loads so the native NSWindow exists.
void forceWindowFullScreenOnScreen(void *handle, int screenIndex) {
    NSView   *view   = (__bridge NSView *)(void *)handle;
    NSWindow *win    = [view window];
    if (!win) { NSLog(@"[OwlSwitch] forceWindowFullScreen: no NSWindow"); return; }

    NSArray<NSScreen *> *screens = [NSScreen screens];
    NSScreen *screen = nil;
    if (screenIndex >= 0 && screenIndex < (int)screens.count)
        screen = screens[screenIndex];
    if (!screen)
        screen = win.screen ?: [NSScreen mainScreen];
    if (!screen) { NSLog(@"[OwlSwitch] forceWindowFullScreen: no NSScreen"); return; }

    NSLog(@"[OwlSwitch] forceWindowFullScreen: index=%d screen.frame = {{%.0f,%.0f},{%.0f,%.0f}}",
          screenIndex,
          screen.frame.origin.x, screen.frame.origin.y,
          screen.frame.size.width, screen.frame.size.height);

    win.styleMask = NSWindowStyleMaskBorderless;
    win.hasShadow = NO;
    [win setFrame:screen.frame display:YES animate:NO];
}
