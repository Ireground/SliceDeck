#include "ReaperBridge.h"
#include <pluginterfaces/base/funknown.h>

namespace
{
    // Layout from REAPER SDK's reaper_vst3_interfaces.h (REAPER 5.02+)
    class IReaperHostApplication : public Steinberg::FUnknown
    {
    public:
        virtual void* PLUGIN_API getReaperApi (Steinberg::CStringA funcname) = 0;
        virtual void* PLUGIN_API getReaperParent (Steinberg::uint32 w) = 0; // 1 track, 2 take, 3 project
        virtual void* PLUGIN_API reaperExtended (Steinberg::uint32 call, void* p1, void* p2, void* p3) = 0;
    };

    const Steinberg::TUID reaperHostIID = INLINE_UID (0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);

    IReaperHostApplication* asReaper (void* p) { return static_cast<IReaperHostApplication*> (p); }
}

ReaperBridge::~ReaperBridge()
{
    if (host != nullptr)
        asReaper (host)->release();
}

void ReaperBridge::setHostContext (Steinberg::FUnknown* hostContext)
{
    if (hostContext == nullptr || host != nullptr)
        return;

    void* obj = nullptr;
    if (hostContext->queryInterface (reaperHostIID, &obj) == Steinberg::kResultOk && obj != nullptr)
        host = obj;   // keep the reference; released in destructor
}

bool ReaperBridge::getSelectedAudioItem (ArrangeItem& out, juce::String& error) const
{
    if (host == nullptr) { error = "REAPER ONLY"; return false; }
    auto* h = asReaper (host);

    using GetSelItemFn   = void* (*) (void* proj, int idx);
    using PtrFn          = void* (*) (void*);
    using BoolFn         = bool (*) (void*);
    using SrcNameFn      = void (*) (void* src, char* buf, int size);
    using InfoFn         = double (*) (void* obj, const char* name);
    using TakeNameFn     = const char* (*) (void* take);

    auto getSel       = reinterpret_cast<GetSelItemFn> (h->getReaperApi ("GetSelectedMediaItem"));
    auto activeTake   = reinterpret_cast<PtrFn>        (h->getReaperApi ("GetActiveTake"));
    auto isMidi       = reinterpret_cast<BoolFn>       (h->getReaperApi ("TakeIsMIDI"));
    auto takeSource   = reinterpret_cast<PtrFn>        (h->getReaperApi ("GetMediaItemTake_Source"));
    auto sourceParent = reinterpret_cast<PtrFn>        (h->getReaperApi ("GetMediaSourceParent"));
    auto sourceName   = reinterpret_cast<SrcNameFn>    (h->getReaperApi ("GetMediaSourceFileName"));
    auto itemInfo     = reinterpret_cast<InfoFn>       (h->getReaperApi ("GetMediaItemInfo_Value"));
    auto takeInfo     = reinterpret_cast<InfoFn>       (h->getReaperApi ("GetMediaItemTakeInfo_Value"));
    auto takeName     = reinterpret_cast<TakeNameFn>   (h->getReaperApi ("GetTakeName"));

    if (getSel == nullptr || activeTake == nullptr || takeSource == nullptr || sourceName == nullptr
        || itemInfo == nullptr || takeInfo == nullptr)
    {
        error = "REAPER TOO OLD";
        return false;
    }

    void* item = getSel (h->getReaperParent (3), 0);
    if (item == nullptr) { error = "SELECT AN ITEM"; return false; }
    void* take = activeTake (item);
    if (take == nullptr) { error = "EMPTY ITEM"; return false; }
    if (isMidi != nullptr && isMidi (take)) { error = "MIDI ITEM"; return false; }

    void* src = takeSource (take);
    char buf[4096] = {};
    for (int depth = 0; src != nullptr && depth < 4; ++depth)
    {
        buf[0] = 0;
        sourceName (src, buf, (int) sizeof (buf));
        if (buf[0] != 0) break;
        src = sourceParent != nullptr ? sourceParent (src) : nullptr;   // section / reversed sources
    }
    const juce::String path = juce::String::fromUTF8 (buf);
    if (path.isEmpty() || ! juce::File::isAbsolutePath (path) || ! juce::File (path).existsAsFile())
    {
        error = "NO AUDIO FILE";
        return false;
    }

    const double rate = juce::jmax (0.0001, takeInfo (take, "D_PLAYRATE"));
    out.file = juce::File (path);
    out.startSec = juce::jmax (0.0, takeInfo (take, "D_STARTOFFS"));
    out.lengthSec = juce::jmax (0.0, itemInfo (item, "D_LENGTH")) * rate;
    out.name = takeName != nullptr && takeName (take) != nullptr ? juce::String::fromUTF8 (takeName (take))
                                                                  : out.file.getFileNameWithoutExtension();
    return true;
}

juce::File ReaperBridge::getProjectFolder() const
{
    if (host == nullptr)
        return {};

    using EnumProjectsFn = void* (*) (int idx, char* fn, int fnSize);
    auto enumProjects = reinterpret_cast<EnumProjectsFn> (asReaper (host)->getReaperApi ("EnumProjects"));
    if (enumProjects == nullptr)
        return {};

    void* myProject = asReaper (host)->getReaperParent (3);
    char buf[4096];

    auto folderFrom = [] (const char* path) -> juce::File
    {
        juce::String s = juce::String::fromUTF8 (path);
        if (s.isEmpty() || ! juce::File::isAbsolutePath (s)) return {};
        return juce::File (s).getParentDirectory();
    };

    if (myProject != nullptr)
    {
        for (int i = 0; i < 512; ++i)
        {
            buf[0] = 0;
            void* p = enumProjects (i, buf, (int) sizeof (buf));
            if (p == nullptr) break;
            if (p == myProject) return folderFrom (buf);
        }
        return {};
    }

    // Fallback: current project tab
    buf[0] = 0;
    if (enumProjects (-1, buf, (int) sizeof (buf)) != nullptr)
        return folderFrom (buf);
    return {};
}
