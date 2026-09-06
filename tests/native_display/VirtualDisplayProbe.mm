// Test-only private CoreGraphics API. Never link this helper into OwlSwitch.
// API reference: Chromium ui/display/mac/test/virtual_display_util_mac.mm.
// Runtime lookup keeps unsupported macOS versions a detectable prerequisite failure.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>

@interface NSObject (OwlSwitchVirtualDisplayTest)
- (id)initWithDescriptor:(id)descriptor;
- (BOOL)applySettings:(id)settings;
- (id)initWithWidth:(unsigned)width height:(unsigned)height refreshRate:(double)rate;
- (unsigned)displayID;
- (void)setSizeInMillimeters:(CGSize)size;
@end

static volatile sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }

int main(int argc, char **argv) {
    if (argc != 4) return 64;
    const int width = std::atoi(argv[1]), height = std::atoi(argv[2]), scale = std::atoi(argv[3]);
    if (width < 640 || width > 3840 || height < 480 || height > 2160 ||
        (scale != 1 && scale != 2)) return 64;
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    @autoreleasepool {
        Class descriptorClass = NSClassFromString(@"CGVirtualDisplayDescriptor");
        Class displayClass = NSClassFromString(@"CGVirtualDisplay");
        Class modeClass = NSClassFromString(@"CGVirtualDisplayMode");
        Class settingsClass = NSClassFromString(@"CGVirtualDisplaySettings");
        if (!descriptorClass || !displayClass || !modeClass || !settingsClass) return 77;
        id descriptor = [[descriptorClass alloc] init];
        [descriptor setValue:@"OwlSwitch virtual display test" forKey:@"name"];
        [descriptor setValue:dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0) forKey:@"queue"];
        [descriptor setValue:@(width * scale) forKey:@"maxPixelsWide"];
        [descriptor setValue:@(height * scale) forKey:@"maxPixelsHigh"];
        // macOS requires a nonzero vendor and distinct serials for concurrent displays.
        [descriptor setValue:@505 forKey:@"vendorID"];
        [descriptor setValue:@166 forKey:@"productID"];
        unsigned serial = arc4random_uniform(0x7ffffffe) + 1;
        [descriptor setValue:@(serial) forKey:@"serialNum"];
        [descriptor setValue:@(serial) forKey:@"serialNumber"];
        [descriptor setSizeInMillimeters:CGSizeMake(width * 25.4 / 96, height * 25.4 / 96)];
        id display = [[displayClass alloc] initWithDescriptor:descriptor];
        if (!display) return 77;
        id settings = [[settingsClass alloc] init];
        id mode = [[modeClass alloc] initWithWidth:width height:height refreshRate:60];
        [settings setValue:@[mode] forKey:@"modes"];
        [settings setValue:@(scale == 2) forKey:@"hiDPI"];
        if (![display applySettings:settings]) return 77;
        unsigned displayID = [display displayID];
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:10];
        while (!stopping && !CGDisplayIsOnline(displayID) && deadline.timeIntervalSinceNow > 0)
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        if (!CGDisplayIsOnline(displayID)) return 77;
        std::printf("{\"displayID\":%u,\"online\":true}\n", displayID);
        std::fflush(stdout);
        // A hard lifetime bound also cleans up after an interrupted test driver.
        deadline = [NSDate dateWithTimeIntervalSinceNow:30];
        while (!stopping && deadline.timeIntervalSinceNow > 0)
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        (void)[display displayID]; // Retain the display until the test ends.
    }
    return 0;
}
