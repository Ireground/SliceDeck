#pragma once

#include <JuceHeader.h>
#include "ReaperBridge.h"

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
};

struct SliceMap
{
    int trimStart = 0, trimEnd = 0;
    std::vector<SliceInfo> slices;   // empty unless in Slice mode
};

struct SliceSetting { float gainDb = 0.0f; float pitch = 0.0f; };

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

    bool loadFile (const juce::File& file, juce::String& error);
    std::shared_ptr<const SampleData> getSampleData() const;
    std::shared_ptr<const SliceMap>   getSliceMap() const;

    juce::String getSampleName() const;
    juce::String getStatusText() const;
    bool isSampleMissing() const;

    void addMarker (int samplePos);
    void removeMarker (int sliceIndex);
    void moveMarker (int sliceIndex, int newPos);
    void setSliceSetting (int sliceIndex, float gainDb, float pitch);
    SliceSetting getSliceSetting (int sliceIndex) const;

    void previewNote (int note, bool on);

    //== Random Play
    static constexpr int MaxRandom = 64;
    static constexpr int RandomNote = 200;      // internal note number for random-play voices
    static double stepBeatsFor (int rateIndex);
    void randomize();                           // pick a new random set of slices
    int getRandomLength() const noexcept { return rndLen.load(); }
    int getRandomSlice (int i) const noexcept { return rndSeq[(size_t) juce::jlimit (0, MaxRandom - 1, i)].load(); }
    int getRandomStep() const noexcept { return rndStep.load(); }
    bool isTransportPlaying() const noexcept { return transportPlaying.load(); }
    juce::File writeMidiFile();
    float getParam (const juce::String& id) const { return param (id); }
    void setParamValue (const juce::String& id, float plain) { setParam (id, plain); }
    int getRootNote() const;
    int getPlayheadSample() const noexcept { return playhead.load(); }
    int getLastTriggeredSlice() const noexcept { return lastSlice.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Voice
    {
        bool active = false, releasing = false, ignoreNoteOff = false, loop = false;
        int note = -1, slice = -1;
        double pos = 0.0, inc = 1.0;
        int start = 0, end = 0, loopStart = 0, fade = 0;
        float gain = 1.0f;
        int stealRemaining = 0;     // >0 while fading out after being stolen
        juce::uint64 age = 0;
        juce::ADSR env;
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
                       const SampleData& data, const SliceMap& map);

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
    int lastRndCount = -1;

    std::array<std::atomic<int>, MaxRandom> rndSeq {};
    std::atomic<int> rndLen { 0 }, rndStep { -1 };
    std::atomic<bool> transportPlaying { false };
    double prevB1 = 0.0;
    bool wasPlaying = false;
    juce::Random rng;
    const SampleData* lastDataSeen = nullptr;   // audio thread

    std::atomic<bool> needsReslice { false }, needsPlaybackRebuild { false };
    std::atomic<double> hostBpm { 120.0 };
    double lastSlicedBpm = 0.0;

    // --- audio thread
    std::array<Voice, 32> voices;
    juce::uint64 voiceCounter = 0;
    double hostRate = 44100.0;
    juce::ADSR::Parameters currentEnv;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::SmoothedValue<float> outGain;
    juce::MidiMessageCollector previewMidi;
    std::atomic<int> playhead { -1 }, lastSlice { -1 };

    juce::AudioFormatManager formats;
    ReaperBridge reaper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceDeckProcessor)
};
