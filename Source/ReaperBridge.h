#pragma once
// Talks to REAPER through its VST3 extension so the plugin can find the
// folder of the project it lives in. In other hosts this simply does nothing.

#include <juce_core/juce_core.h>

namespace Steinberg { class FUnknown; }

class ReaperBridge
{
public:
    ReaperBridge() = default;
    ~ReaperBridge();

    void setHostContext (Steinberg::FUnknown* hostContext);
    bool isReaper() const noexcept { return host != nullptr; }

    // Folder containing the saved .rpp file. Invalid File if unknown/unsaved.
    juce::File getProjectFolder() const;

    // The first selected audio item on the timeline: its source file and the
    // part of that file the item plays (in source seconds).
    struct ArrangeItem
    {
        juce::File file;
        double startSec = 0.0, lengthSec = 0.0;
        juce::String name;
    };
    bool getSelectedAudioItem (ArrangeItem& out, juce::String& error) const;

private:
    void* host = nullptr;   // reaper::IReaperHostApplication*
    JUCE_DECLARE_NON_COPYABLE (ReaperBridge)
};
