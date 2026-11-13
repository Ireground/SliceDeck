#pragma once

#include <JuceHeader.h>
#include "ReaperBridge.h"

namespace signalsmith { namespace stretch { template <typename Sample, class RandomEngine> struct SignalsmithStretch; } }

// Audio that the voices play (already reversed if Reverse is on). Immutable once built.
struct SampleData
{
    juce::AudioBuffer<float> audio;       // always 2 channels
    std::vector<float> mono;              // for analysis / zero snapping
    double sampleRate = 44100.0;
    std::vector<float> peakMin, peakMax;  // waveform overview
    int length() const { return audio.getNumSamples(); }
};

struct SliceInfo
{
    int start = 0, end = 0;
    float gainDb = 0.0f;
    float pitch = 0.0f;   // semitones
    bool loop = false;
};

struct SliceMap
{
    int trimStart = 0, trimEnd = 0;
    std::vector<SliceInfo> slices;   // empty unless in Slice mode
};

struct SliceSetting { float gainDb = 0.0f; float pitch = 0.0f; bool loop = false; };

class SliceDeckProcessor : public juce::AudioProcessor,
                           public juce::VST3ClientExtensions,
                           public juce::ChangeBroadcaster,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::Timer
{
public:
    SliceDeckProcessor();
    ~SliceDeckProcessor() override;

    //== AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== VST3 / REAPER
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    void setIHostApplication (Steinberg::FUnknown* host) override { reaper.setHostContext (host); }

    //== Used by the editor (message thread)
    enum Mode { modeClassic = 0, modeOneShot, modeSlice };
    enum SliceBy { byTransient = 0, byBeat, byRegion, byManual };
    enum Engine { engineRepitch = 0, engineStretch };
    enum TrigBy { trigNote = 0, trigVelocity, trigSelect };

    bool loadFile (const juce::File& file, juce::String& error);
    std::shared_ptr<const SampleData> getSampleData() const;
    std::shared_ptr<const SliceMap>   getSliceMap() const;

    juce::String getSampleName() const;
    juce::String getStatusText() const;
    bool isSampleMissing() const;

    void addMarker (int samplePos);
    void removeMarker (int sliceIndex);
    void moveMarker (int sliceIndex, int newPos);
    void setSliceSetting (int sliceIndex, SliceSetting setting);
    SliceSetting getSliceSetting (int sliceIndex) const;

    void previewNote (int note, bool on);
    int getRootNote() const;
    int getPlayheadSample() const noexcept { return playhead.load(); }
    int getLastTriggeredSlice() const noexcept { return lastSlice.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Voice
    {
        bool active = false, releasing = false, ignoreNoteOff = false, loop = false;
        int engine = engineRepitch;
        bool frozen = false;
        int note = -1, slice = -1;
        float noteSemis = 0.0f;     // pitch from key + slice (without global transpose)
        double pos = 0.0, inc = 1.0;
        int start = 0, end = 0, loopStart = 0, fade = 0;
        float gain = 1.0f;
        int stealRemaining = 0;     // >0 while fading out after being stolen
        juce::uint64 age = 0;
        juce::ADSR env;

        // freeze grains
        double grainAnchor[2] {};
        double grainPhase[2] {};

        // stretch
        int index = 0;
        double inFrac = 0.0;
        bool inputEnded = false;
        int tailRemaining = 0;

        // character
        float lpL = 0, lpR = 0, holdL = 0, holdR = 0, holdPhase = 1.0f;
    };

    struct BlockSettings
    {
        float transpose = 0, speed = 1, freezePos = 0, freezeSize = 0.06f;
        bool freeze = false;
        int character = 0; float charAmount = 0;
        double rateRatio = 1, stretchRate = 1;
        float formant = 0; bool keepFormants = false;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;

    void rebuildPlayback();          // original audio -> SampleData (reverse etc.)
    void rebuildSlices();            // SampleData + params -> SliceMap
    void convertToManual();
    void setParam (const juce::String& id, float plainValue);
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    void handleNoteOff (int note);
    Voice* allocateVoice();
    void stealVoice (Voice& v);
    void renderVoices (juce::AudioBuffer<float>& out, int start, int num,
                       const SampleData& data, const BlockSettings& bs);
    void renderVoiceChunk (Voice& v, float* l, float* r, int num, const SampleData& data, const BlockSettings& bs);
    int fetchInput (Voice& v, const SampleData& data, float* l, float* r, int n, int dir);
    void startStretch (Voice& v, const SampleData& data, const BlockSettings& bs);
    void applyStretchPitch (Voice& v, const SampleData& data, const BlockSettings& bs);
    void applyCharacter (Voice& v, float* l, float* r, int num, const BlockSettings& bs);

    juce::String encodeAudio() const;
    bool decodeAudio (const juce::String& base64);
    bool tryResolveMissing();

    // --- data shared with the audio thread
    mutable juce::SpinLock dataLock;
    std::shared_ptr<const SampleData> sampleData;
    std::shared_ptr<const SliceMap>   sliceMap;
    std::vector<std::shared_ptr<const SampleData>> retiredData;   // freed on message thread

    // --- message-thread state (guarded by stateLock)
    mutable juce::CriticalSection stateLock;
    juce::AudioBuffer<float> originalAudio;
    double originalRate = 44100.0;
    juce::String sampleName, absolutePath, relativePath;
    bool missing = false, playbackReversed = false;
    std::vector<int> manualPoints;
    std::vector<SliceSetting> sliceSettings;
    int missingRetries = 0;
    int timerTick = 0;
    const SampleData* lastDataSeen = nullptr;   // audio thread

    std::atomic<bool> needsReslice { false }, needsPlaybackRebuild { false };
    std::atomic<double> hostBpm { 120.0 };
    double lastSlicedBpm = 0.0;
    double stretchSetupRate = 0.0;

    // --- audio thread
    std::array<Voice, 32> voices;
    juce::uint64 voiceCounter = 0;
    double hostRate = 44100.0;
    juce::ADSR::Parameters currentEnv;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::SmoothedValue<float> outGain;
    juce::MidiMessageCollector previewMidi;
    std::atomic<int> playhead { -1 }, lastSlice { -1 };
    std::vector<std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float, void>>> stretchers;
    std::vector<float> scratchL, scratchR, voiceL, voiceR, inL, inR;

    juce::AudioFormatManager formats;
    ReaperBridge reaper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceDeckProcessor)
};
