#include "MacWindow.h"
#import <Cocoa/Cocoa.h>

void makeTitleBarTransparent (juce::Component& window)
{
    auto* peer = window.getPeer();
    if (peer == nullptr)
        return;
    auto* view = (NSView*) peer->getNativeHandle();
    NSWindow* ns = view != nil ? [view window] : nil;
    if (ns == nil)
        return;

    const auto w = window.getWidth(), h = window.getHeight();   // the content size, without a title bar
    ns.styleMask |= NSWindowStyleMaskFullSizeContentView;
    ns.titlebarAppearsTransparent = YES;
    ns.titleVisibility = NSWindowTitleHidden;
    [ns setContentSize: NSMakeSize (w, h)];                      // the content now fills the title bar's strip too: the frame shrinks by it
}
