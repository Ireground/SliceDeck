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

private:
    void* host = nullptr;   // reaper::IReaperHostApplication*
    JUCE_DECLARE_NON_COPYABLE (ReaperBridge)
};
