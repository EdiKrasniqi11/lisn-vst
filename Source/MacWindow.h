#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The desktop app's window on the Mac (it needs the window on screen): macOS's own red / yellow / green buttons stay, drawn over
// the header, and the window keeps its content size. No title text, no title bar background.
void makeTitleBarTransparent (juce::Component& window);
