#include "macintegration.h"

#import <AppKit/AppKit.h>

namespace {
NSURL *defaultBrowserUrl() {
    return [[NSWorkspace sharedWorkspace]
        URLForApplicationToOpenURL:[NSURL URLWithString:@"https://example.com"]];
}
}

QString MacIntegration::defaultBrowserId() {
    @autoreleasepool {
        NSURL *app = defaultBrowserUrl();
        NSString *identifier = app ? [NSBundle bundleWithURL:app].bundleIdentifier : nil;
        return identifier ? QString::fromNSString(identifier) : QString();
    }
}

QString MacIntegration::defaultBrowserPath() {
    @autoreleasepool {
        NSURL *app = defaultBrowserUrl();
        return app ? QString::fromNSString(app.path) : QString();
    }
}

void MacIntegration::activateApp() {
    @autoreleasepool {
        [NSApp activate];
    }
}
