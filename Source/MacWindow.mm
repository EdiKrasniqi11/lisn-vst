#include "MacWindow.h"
#import <Cocoa/Cocoa.h>

namespace
{
// The header row's centre, from the window's top: the header sits at y 16 and is 44 high.
constexpr CGFloat rowCentre = 38.0;

// macOS puts the three lights in a 28 px strip at the window's top, centred in it. Make that strip tall enough to hold them at the
// header row's centre and move them there (they only take clicks inside their parent's frame). AppKit lays them out again now
// and then, so this runs again on those events; it changes nothing when the lights are already in place.
void alignLights (NSWindow* ns)
{
    NSView* bar = [ns standardWindowButton: NSWindowCloseButton].superview;   // the title bar view
    NSView* container = bar.superview;                                        // pinned to the window frame's top
    NSView* frameView = container.superview;
    if (bar == nil || container == nil || frameView == nil)
        return;

    const CGFloat height = rowCentre + 10.0;   // the lights' bottoms, 7 px under the centre, and a little more
    const NSRect wanted = NSMakeRect (container.frame.origin.x, NSMaxY (frameView.bounds) - height, container.frame.size.width, height);
    if (! NSEqualRects (container.frame, wanted))
        container.frame = wanted;
    if (! NSEqualRects (bar.frame, container.bounds))
        bar.frame = container.bounds;

    for (const auto kind : { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton })
    {
        NSButton* b = [ns standardWindowButton: kind];
        const CGFloat y = bar.isFlipped ? rowCentre - b.frame.size.height / 2 : height - rowCentre - b.frame.size.height / 2;
        if (b != nil && b.frame.origin.y != y)
            [b setFrameOrigin: NSMakePoint (b.frame.origin.x, y)];
    }
}
}

bool makeTitleBarTransparent (juce::Component& window)
{
    auto* peer = window.getPeer();
    if (peer == nullptr)
        return false;
    auto* view = (NSView*) peer->getNativeHandle();
    NSWindow* ns = view != nil ? [view window] : nil;
    if (ns == nil)
        return false;

    const auto w = window.getWidth(), h = window.getHeight();   // the content size, without a title bar
    ns.styleMask |= NSWindowStyleMaskFullSizeContentView;
    ns.titlebarAppearsTransparent = YES;
    ns.titleVisibility = NSWindowTitleHidden;
    [ns setContentSize: NSMakeSize (w, h)];                      // the content now fills the title bar's strip too: the frame shrinks by it

    alignLights (ns);
    NSNotificationCenter* centre = [NSNotificationCenter defaultCenter];
    void (^again) (NSNotification*) = ^(NSNotification*) { alignLights (ns); };
    for (NSString* name in @[ NSWindowDidBecomeKeyNotification, NSWindowDidResignKeyNotification, NSWindowDidResizeNotification,
                              NSWindowDidChangeScreenNotification, NSWindowDidChangeBackingPropertiesNotification ])
        [centre addObserverForName: name object: ns queue: nil usingBlock: again];
    NSView* bar = [ns standardWindowButton: NSWindowCloseButton].superview;
    for (NSView* v in @[ bar, bar.superview ])   // AppKit moving the strip or the lights back
        if (v != nil)
        {
            v.postsFrameChangedNotifications = YES;
            [centre addObserverForName: NSViewFrameDidChangeNotification object: v queue: nil usingBlock: again];
        }
    dispatch_async (dispatch_get_main_queue(), ^{ alignLights (ns); });   // after the window's first layout
    return true;
}
