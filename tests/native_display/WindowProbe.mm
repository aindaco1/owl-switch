// Test-only inspection of WindowServer and accessibility state for owned PIDs.
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <cstdio>
#include <initializer_list>
#include <csignal>
#include <cmath>

static volatile sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }

static id attribute(AXUIElementRef element, CFStringRef name,
                    NSMutableArray *errors = nil, bool optional = false) {
    CFTypeRef value = nullptr;
    AXError error = AXUIElementCopyAttributeValue(element, name, &value);
    if (error != kAXErrorSuccess && !(optional &&
            (error == kAXErrorNoValue || error == kAXErrorAttributeUnsupported)))
        [errors addObject:@{@"attribute": (__bridge NSString *)name, @"error": @(error)}];
    return CFBridgingRelease(value);
}

static NSDictionary *axFrame(AXUIElementRef element, NSMutableArray *errors) {
    id position = attribute(element, kAXPositionAttribute, errors);
    id size = attribute(element, kAXSizeAttribute, errors);
    CGPoint point = {}; CGSize dimensions = {};
    bool valid = position && size &&
        CFGetTypeID((__bridge CFTypeRef)position) == AXValueGetTypeID() &&
        CFGetTypeID((__bridge CFTypeRef)size) == AXValueGetTypeID() &&
        AXValueGetValue((__bridge AXValueRef)position, kAXValueTypeCGPoint, &point) &&
        AXValueGetValue((__bridge AXValueRef)size, kAXValueTypeCGSize, &dimensions) &&
        std::isfinite(point.x) && std::isfinite(point.y) &&
        std::isfinite(dimensions.width) && std::isfinite(dimensions.height) &&
        dimensions.width > 0 && dimensions.height > 0;
    if (!valid) [errors addObject:@{@"attribute": @"frame", @"error": @"invalid AX geometry"}];
    return @{@"X": @(point.x), @"Y": @(point.y),
             @"Width": @(dimensions.width), @"Height": @(dimensions.height), @"valid": @(valid)};
}

static NSArray *windowsForPID(pid_t pid) {
    NSMutableArray *windows = [NSMutableArray array];
    NSArray *all = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    for (NSDictionary *window in all) {
        if ([window[(__bridge NSString *)kCGWindowOwnerPID] intValue] != pid) continue;
        [windows addObject:@{
            @"id": window[(__bridge NSString *)kCGWindowNumber],
            @"frame": window[(__bridge NSString *)kCGWindowBounds],
            @"layer": window[(__bridge NSString *)kCGWindowLayer],
            @"alpha": window[(__bridge NSString *)kCGWindowAlpha]}];
    }
    return windows;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        // Inspection and key delivery must never activate the probe itself.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        if (argc < 2) return 64;
        NSString *command = @(argv[1]);
        NSMutableDictionary *result = [NSMutableDictionary dictionary];
        if ([command isEqual:@"permissions"]) {
            result[@"accessibility"] = @(AXIsProcessTrusted());
            result[@"capture"] = @(CGPreflightScreenCaptureAccess());
        } else if ([command isEqual:@"sentinel"]) {
            // A real second app used to represent a deliberate switch while loading.
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 240, 100)
                styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
            window.title = @"OwlSwitch focus test";
            [window makeKeyAndOrderFront:nil];
            std::signal(SIGTERM, stop);
            std::signal(SIGINT, stop);
            std::printf("{\"pid\":%d}\n", getpid());
            std::fflush(stdout);
            NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:600];
            while (!stopping && deadline.timeIntervalSinceNow > 0)
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            [window close];
            return 0;
        } else {
            if (argc < 3) return 64;
            pid_t pid = atoi(argv[2]);
            if (pid <= 1) return 64;
            if ([command isEqual:@"trace"]) {
                NSMutableArray *changes = [NSMutableArray array];
                NSArray *previous = nil;
                pid_t previousFront = -1;
                NSDate *start = [NSDate date];
                NSTimeInterval firstVisible = -1;
                // A cold signed/helper launch can take longer than the observation
                // period. Keep watching startup, then capture three seconds from
                // the first visible window rather than expiring before it exists.
                while (-start.timeIntervalSinceNow < (firstVisible < 0 ? 20 : firstVisible + 3)) {
                    NSArray *current = windowsForPID(pid);
                    if (firstVisible < 0) {
                        for (NSDictionary *window in current) {
                            if ([window[@"layer"] intValue] == 0 && [window[@"alpha"] doubleValue] > 0) {
                                firstVisible = -start.timeIntervalSinceNow;
                                break;
                            }
                        }
                    }
                    pid_t front = [NSWorkspace sharedWorkspace].frontmostApplication.processIdentifier;
                    if (![current isEqual:previous] || front != previousFront) {
                        [changes addObject:@{@"elapsed": @(-start.timeIntervalSinceNow), @"windows": current, @"frontPID": @(front)}];
                        previous = current;
                        previousFront = front;
                    }
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
                }
                result[@"changes"] = changes;
                result[@"firstVisibleElapsed"] = firstVisible < 0 ? (id)[NSNull null] : @(firstVisible);
                result[@"elapsed"] = @(-start.timeIntervalSinceNow);
            } else if ([command isEqual:@"activate"]) {
                NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
                result[@"activated"] = @([app activateWithOptions:0]);
                AXUIElementRef application = AXUIElementCreateApplication(pid);
                NSArray *windows = attribute(application, kAXWindowsAttribute);
                if (windows.count) AXUIElementPerformAction((__bridge AXUIElementRef)windows[0], kAXRaiseAction);
                CFRelease(application);
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
            } else if ([command isEqual:@"key"] && argc == 4) {
                if (!AXIsProcessTrusted()) return 77;
                for (BOOL down : {YES, NO}) {
                    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, atoi(argv[3]), down);
                    CGEventSetFlags(event, 0);
                    CGEventPostToPid(pid, event);
                    CFRelease(event);
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
                }
            } else if ([command isEqual:@"snapshot"]) {
                // Bound each remote AX request and record failures instead of
                // silently treating unreadable attributes as absent chrome.
                AXUIElementRef system = AXUIElementCreateSystemWide();
                AXUIElementSetMessagingTimeout(system, 0.5);
                CFRelease(system);
                NSMutableArray *errors = [NSMutableArray array];
                result[@"observerPID"] = @(getpid());
                result[@"frontPID"] = @([NSWorkspace sharedWorkspace].frontmostApplication.processIdentifier);
                result[@"windows"] = windowsForPID(pid);
                AXUIElementRef app = AXUIElementCreateApplication(pid);
                NSMutableArray *accessible = [NSMutableArray array];
                for (id item in attribute(app, kAXWindowsAttribute, errors)) {
                    AXUIElementRef window = (__bridge AXUIElementRef)item;
                    NSMutableDictionary *details = [NSMutableDictionary dictionaryWithDictionary:axFrame(window, errors)];
                    NSMutableArray *buttons = [NSMutableArray array];
                    for (NSString *name in @[@"AXCloseButton", @"AXMinimizeButton", @"AXZoomButton"]) {
                        id button = attribute(window, (__bridge CFStringRef)name, errors, true);
                        if (button) [buttons addObject:@{@"kind": name,
                            @"frame": axFrame((__bridge AXUIElementRef)button, errors)}];
                    }
                    details[@"buttons"] = buttons;
                    [accessible addObject:details];
                }
                CFRelease(app);
                result[@"accessibleWindows"] = accessible;
                result[@"accessibilityErrors"] = errors;
            } else return 64;
        }
        NSData *json = [NSJSONSerialization dataWithJSONObject:result options:0 error:nil];
        std::puts([[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
    }
}
