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
