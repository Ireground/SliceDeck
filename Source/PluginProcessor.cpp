#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SliceEngine.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace
{
    const double divisionBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    constexpr int maxSlices = 128;
    constexpr int minSliceLen = 16;

    bool readAudioFile (juce::AudioFormatManager& fm, const juce::File& f,
                        juce::AudioBuffer<float>& dest, double& rate, juce::String& error)
    {
        if (! f.existsAsFile()) { error = "File not found"; return false; }
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) { error = "Unsupported audio format"; return false; }
        if (r->lengthInSamples <= 0 || r->sampleRate <= 0) { error = "File is empty"; return false; }
        if ((double) r->lengthInSamples / r->sampleRate > 600.0) { error = "File is longer than 10 minutes"; return false; }

        const int len = (int) r->lengthInSamples;
        const int ch = (int) juce::jlimit (1u, 2u, r->numChannels);
        dest.setSize (ch, len);
        r->read (&dest, 0, len, 0, true, ch > 1);
        rate = r->sampleRate;
        return true;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SliceDeckProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withLabel ("%");
    auto ms  = AudioParameterFloatAttributes().withLabel ("ms");
    auto db  = AudioParameterFloatAttributes().withLabel ("dB");

    auto timeRange = [] (float maxMs, float centre)
    {
        NormalisableRange<float> r (0.0f, maxMs, 0.01f);
        r.setSkewForCentre (centre);
        return r;
    };

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Classic", "1-Shot", "Slice" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "playback", 1 }, "Playback", StringArray { "Trigger", "Gate" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "sliceBy", 1 }, "Slice By", StringArray { "Transient", "Beat", "Region", "Manual" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "sensitivity", 1 }, "Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "division", 1 }, "Division", StringArray { "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32" }, 4));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "regions", 1 }, "Regions", 2, 64, 8));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "voices", 1 }, "Voices", 1, 16, 8));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "root", 1 }, "Root Note", 0, 127, 60));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "firstNote", 1 }, "First Slice Note", 0, 127, 36));

    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "start", 1 }, "Start", NormalisableRange<float> (0.0f, 1.0f, 0.00001f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "end", 1 }, "End", NormalisableRange<float> (0.0f, 1.0f, 0.00001f), 1.0f));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "loop", 1 }, "Loop", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "loopStart", 1 }, "Loop Start", NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "fade", 1 }, "Loop Fade", timeRange (500.0f, 30.0f), 10.0f, ms));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "reverse", 1 }, "Reverse", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "snap", 1 }, "Snap", true));

    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "attack", 1 }, "Attack", timeRange (5000.0f, 100.0f), 0.5f, ms));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "decay", 1 }, "Decay", timeRange (10000.0f, 500.0f), 600.0f, ms));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "sustain", 1 }, "Sustain", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "release", 1 }, "Release", timeRange (10000.0f, 300.0f), 50.0f, ms));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "vel", 1 }, "Velocity", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));

    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "filterOn", 1 }, "Filter", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "filterType", 1 }, "Filter Type", StringArray { "Low-pass", "High-pass", "Band-pass" }, 0));
    NormalisableRange<float> freq (20.0f, 20000.0f, 0.1f); freq.setSkewForCentre (1000.0f);
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "cutoff", 1 }, "Cutoff", freq, 8000.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "reso", 1 }, "Resonance", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));

    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "pan", 1 }, "Pan", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "transpose", 1 }, "Transpose", -48, 48, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "detune", 1 }, "Detune", NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("ct")));

    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "embed", 1 }, "Embed Sample", true));

    // v2: playback engine
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "engine", 2 }, "Engine", StringArray { "Repitch", "Stretch" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "speed", 2 }, "Speed", NormalisableRange<float> (-400.0f, 400.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "character", 2 }, "Character", StringArray { "Clean", "Analog", "Digital" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "charAmount", 2 }, "Character Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "freeze", 2 }, "Freeze", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "freezePos", 2 }, "Freeze Position", NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "freezeSize", 2 }, "Freeze Size", timeRange (500.0f, 60.0f), 60.0f, ms));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "sync", 2 }, "Sync to Tempo", true));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "sampleBpm", 2 }, "Sample BPM", NormalisableRange<float> (40.0f, 240.0f, 0.01f), 120.0f, AudioParameterFloatAttributes().withLabel ("BPM")));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "formant", 2 }, "Formant", NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, AudioParameterFloatAttributes().withLabel ("st")));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "keepFormants", 2 }, "Keep Formants", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "trigBy", 2 }, "Trigger By", StringArray { "Note", "Velocity", "Select" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "select", 2 }, "Select", NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f, pct));
    return l;
}

SliceDeckProcessor::SliceDeckProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();
    for (int i = 0; i < (int) voices.size(); ++i) voices[(size_t) i].index = i;

    for (auto* id : { "mode", "sliceBy", "sensitivity", "division", "regions", "start", "end", "snap" })
        apvts.addParameterListener (id, this);
    apvts.addParameterListener ("reverse", this);

    startTimerHz (30);
}

SliceDeckProcessor::~SliceDeckProcessor()
{
    stopTimer();
    for (auto* id : { "mode", "sliceBy", "sensitivity", "division", "regions", "start", "end", "snap", "reverse" })
        apvts.removeParameterListener (id, this);
}

bool SliceDeckProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SliceDeckProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    for (auto& v : voices) { v.active = false; v.env.setSampleRate (sampleRate); }

    // one time-stretcher per voice
    if (stretchers.size() != voices.size() || std::abs (stretchSetupRate - sampleRate) > 1.0)
    {
        stretchers.clear();
        for (size_t i = 0; i < voices.size(); ++i)
        {
            auto st = std::make_unique<signalsmith::stretch::SignalsmithStretch<float, void>> ((long) (i + 1));
            st->presetDefault (2, (float) sampleRate);
            stretchers.push_back (std::move (st));
        }
        stretchSetupRate = sampleRate;
    }
    const int chunk = juce::jmax (512, samplesPerBlock);
    voiceL.assign ((size_t) chunk, 0.0f);
    voiceR.assign ((size_t) chunk, 0.0f);
    inL.assign ((size_t) chunk * 17, 0.0f);
    inR.assign ((size_t) chunk * 17, 0.0f);
    scratchL.assign ((size_t) (sampleRate * 4.0), 0.0f);
    scratchR.assign ((size_t) (sampleRate * 4.0), 0.0f);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    filter.prepare (spec);
    filter.reset();
    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("volume")));
    previewMidi.reset (sampleRate);
    currentEnv = {};   // force envelope update
}

//==============================================================================
void SliceDeckProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "reverse") needsPlaybackRebuild = true;
    else                 needsReslice = true;
}

void SliceDeckProcessor::timerCallback()
{
    // free old sample data once the audio thread no longer uses it
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        retiredData.erase (std::remove_if (retiredData.begin(), retiredData.end(),
                                           [] (auto& p) { return p.use_count() <= 1; }),
                           retiredData.end());
    }

    if (needsPlaybackRebuild.exchange (false))
    {
        needsReslice = false;
        rebuildPlayback();
    }
    else if (needsReslice.exchange (false))
    {
        rebuildSlices();
    }
    else if ((int) param ("mode") == modeSlice && (int) param ("sliceBy") == byBeat
             && std::abs (hostBpm.load() - lastSlicedBpm) > 0.01)
    {
        rebuildSlices();
    }

    // Retry finding a missing file for a few seconds after the project loads
    bool retry = false;
    {
        const juce::ScopedLock sl (stateLock);
        retry = missing && missingRetries < 10;
    }
    if (retry && (++timerTick % 15) == 0)
    {
        { const juce::ScopedLock sl (stateLock); ++missingRetries; }
        if (tryResolveMissing())
        {
            rebuildPlayback();
            sendChangeMessage();
        }
    }
}

void SliceDeckProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

//==============================================================================
std::shared_ptr<const SampleData> SliceDeckProcessor::getSampleData() const
{
    const juce::SpinLock::ScopedLockType sl (dataLock);
    return sampleData;
}

std::shared_ptr<const SliceMap> SliceDeckProcessor::getSliceMap() const
{
    const juce::SpinLock::ScopedLockType sl (dataLock);
    return sliceMap;
}

void SliceDeckProcessor::rebuildPlayback()
{
    const bool rev = param ("reverse") > 0.5f;
    std::shared_ptr<SampleData> d;

    {
        const juce::ScopedLock sl (stateLock);
        const int len = originalAudio.getNumSamples();

        if (len > 0)
        {
            d = std::make_shared<SampleData>();
            d->sampleRate = originalRate;
            d->audio.setSize (2, len);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = originalAudio.getReadPointer (juce::jmin (ch, originalAudio.getNumChannels() - 1));
                float* dst = d->audio.getWritePointer (ch);
                if (rev) for (int i = 0; i < len; ++i) dst[i] = src[len - 1 - i];
                else     std::copy (src, src + len, dst);
            }
        }

        if (rev != playbackReversed)
        {
            // keep manual slices on the same audio when flipping direction
            for (auto& p : manualPoints) p = len - p;
            std::sort (manualPoints.begin(), manualPoints.end());
            std::reverse (sliceSettings.begin(), sliceSettings.end());
            playbackReversed = rev;
        }
    }

    if (d != nullptr)
    {
        const int len = d->length();
        d->mono.resize ((size_t) len);
        auto* l = d->audio.getReadPointer (0);
        auto* r = d->audio.getReadPointer (1);
        for (int i = 0; i < len; ++i) d->mono[(size_t) i] = 0.5f * (l[i] + r[i]);

        const int bins = juce::jmin (2048, len);
        d->peakMin.assign ((size_t) bins, 0.0f);
        d->peakMax.assign ((size_t) bins, 0.0f);
        for (int b = 0; b < bins; ++b)
        {
            const int s0 = (int) ((juce::int64) len * b / bins);
            const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) len * (b + 1) / bins));
            float mn = 0.0f, mx = 0.0f;
            for (int i = s0; i < s1; ++i)
            {
                mn = juce::jmin (mn, l[i], r[i]);
                mx = juce::jmax (mx, l[i], r[i]);
            }
            d->peakMin[(size_t) b] = mn;
            d->peakMax[(size_t) b] = mx;
        }
    }

    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        if (sampleData != nullptr) retiredData.push_back (sampleData);
        sampleData = d;
    }

    rebuildSlices();
}

void SliceDeckProcessor::rebuildSlices()
{
    auto d = getSampleData();
    auto m = std::make_shared<SliceMap>();

    if (d != nullptr && d->length() > minSliceLen * 4)
    {
        const int len = d->length();
        const bool snap = param ("snap") > 0.5f;
        int s = (int) (param ("start") * (float) len);
        int e = (int) (param ("end") * (float) len);
        if (e < s) std::swap (s, e);
        if (e - s < 64) { e = juce::jmin (len, s + 64); s = juce::jmax (0, e - 64); }
        if (snap)
        {
            s = SliceEngine::snapToZero (d->mono.data(), len, d->sampleRate, s);
            e = SliceEngine::snapToZero (d->mono.data(), len, d->sampleRate, e);
            if (e <= s) e = juce::jmin (len, s + 64);
        }
        m->trimStart = s;
        m->trimEnd = e;

        if ((int) param ("mode") == modeSlice)
        {
            std::vector<int> pts;
            const int by = (int) param ("sliceBy");

            if (by == byTransient)
                pts = SliceEngine::transients (d->mono.data(), len, d->sampleRate, s, e, param ("sensitivity") / 100.0f);
            else if (by == byBeat)
                pts = SliceEngine::beats (d->sampleRate, s, e, hostBpm.load(),
                                          divisionBeats[juce::jlimit (0, 5, (int) param ("division"))]);
            else if (by == byRegion)
                pts = SliceEngine::regions (s, e, (int) param ("regions"));
            else
            {
                const juce::ScopedLock sl (stateLock);
                pts.push_back (s);
                for (int p : manualPoints)
                    if (p > s + minSliceLen && p < e - minSliceLen)
                        pts.push_back (p);
            }

            if (snap && by != byManual)
                for (size_t i = 1; i < pts.size(); ++i)
                    pts[i] = SliceEngine::snapToZero (d->mono.data(), len, d->sampleRate, pts[i]);

            std::sort (pts.begin(), pts.end());
            std::vector<int> clean;
            for (int p : pts)
                if (clean.empty() || p - clean.back() >= minSliceLen)
                    clean.push_back (p);
            if (clean.empty()) clean.push_back (s);
            if ((int) clean.size() > maxSlices) clean.resize ((size_t) maxSlices);

            const juce::ScopedLock sl (stateLock);
            if (sliceSettings.size() < clean.size()) sliceSettings.resize (clean.size());
            for (size_t i = 0; i < clean.size(); ++i)
            {
                SliceInfo si;
                si.start = clean[i];
                si.end = i + 1 < clean.size() ? clean[i + 1] : e;
                si.gainDb = sliceSettings[i].gainDb;
                si.pitch = sliceSettings[i].pitch;
                si.loop = sliceSettings[i].loop;
                m->slices.push_back (si);
            }
        }
    }

    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        sliceMap = m;
    }
    lastSlicedBpm = hostBpm.load();
    sendChangeMessage();
}

//==============================================================================
bool SliceDeckProcessor::loadFile (const juce::File& file, juce::String& error)
{
    juce::AudioBuffer<float> buf;
    double rate = 44100.0;
    if (! readAudioFile (formats, file, buf, rate, error))
        return false;

    bool relink = false;
    {
        const juce::ScopedLock sl (stateLock);
        relink = missing;      // relinking keeps slice markers and settings
        originalAudio = std::move (buf);
        originalRate = rate;
        sampleName = file.getFileName();
        absolutePath = file.getFullPathName();
        relativePath.clear();
        missing = false;
        if (! relink)
        {
            manualPoints.clear();
            sliceSettings.clear();
            playbackReversed = param ("reverse") > 0.5f;
        }
    }

    if (! relink)
    {
        setParam ("start", 0.0f);
        setParam ("end", 1.0f);

        // Guess the loop tempo: a whole number of beats (power of two) between 80 and 160 BPM
        const double seconds = (double) originalAudio.getNumSamples() / rate;
        double guess = 120.0;
        for (double beats = 1.0; beats <= 256.0; beats *= 2.0)
        {
            const double bpm = beats * 60.0 / seconds;
            if (bpm >= 80.0 && bpm < 160.0) { guess = bpm; break; }
        }
        setParam ("sampleBpm", (float) juce::jlimit (40.0, 240.0, guess));
    }

    needsReslice = false;
    rebuildPlayback();
    return true;
}

juce::String SliceDeckProcessor::getSampleName() const
{
    const juce::ScopedLock sl (stateLock);
    return sampleName;
}

bool SliceDeckProcessor::isSampleMissing() const
{
    const juce::ScopedLock sl (stateLock);
    return missing;
}

juce::String SliceDeckProcessor::getStatusText() const
{
    juce::String name, abs;
    bool isMissing, hasAudio;
    {
        const juce::ScopedLock sl (stateLock);
        name = sampleName; abs = absolutePath; isMissing = missing;
        hasAudio = originalAudio.getNumSamples() > 0;
    }

    if (isMissing)  return "MISSING: " + name + "  -  drop the file here to relink";
    if (! hasAudio) return "Drop an audio file here, or click Load";
    if (param ("embed") > 0.5f) return "Sample is embedded in the project";
    if (abs.isEmpty()) return "Embedding is off and this sample has no file - turn Embed on";

    const auto folder = reaper.getProjectFolder();
    if (folder.isDirectory() && juce::File (abs).isAChildOf (folder))
        return "Saved as relative path: " + juce::File (abs).getRelativePathFrom (folder).replaceCharacter ('\\', '/');
    if (reaper.isReaper())
        return "Saved as absolute path (move the file into the project folder for a relative path)";
    return "Saved as absolute path";
}

//==============================================================================
void SliceDeckProcessor::convertToManual()
{
    if ((int) param ("sliceBy") == byManual)
        return;

    auto map = getSliceMap();
    {
        const juce::ScopedLock sl (stateLock);
        manualPoints.clear();
        if (map != nullptr)
            for (size_t i = 1; i < map->slices.size(); ++i)
                manualPoints.push_back (map->slices[i].start);
    }
    setParam ("sliceBy", (float) byManual);
}

void SliceDeckProcessor::addMarker (int pos)
{
    auto d = getSampleData();
    if (d == nullptr || (int) param ("mode") != modeSlice) return;
    convertToManual();
    auto map = getSliceMap();
    if (map == nullptr || pos <= map->trimStart + minSliceLen || pos >= map->trimEnd - minSliceLen) return;
    if (param ("snap") > 0.5f) pos = SliceEngine::snapToZero (d->mono.data(), d->length(), d->sampleRate, pos);

    int parent = 0;
    for (size_t i = 0; i < map->slices.size(); ++i)
        if (pos >= map->slices[i].start) parent = (int) i;

    {
        const juce::ScopedLock sl (stateLock);
        for (int p : manualPoints) if (std::abs (p - pos) < minSliceLen) return;
        manualPoints.insert (std::upper_bound (manualPoints.begin(), manualPoints.end(), pos), pos);
        if ((int) sliceSettings.size() <= parent) sliceSettings.resize ((size_t) parent + 1);
        sliceSettings.insert (sliceSettings.begin() + parent + 1, sliceSettings[(size_t) parent]);
    }
    rebuildSlices();
}

void SliceDeckProcessor::removeMarker (int sliceIndex)
{
    if (sliceIndex < 1) return;
    convertToManual();
    auto map = getSliceMap();
    if (map == nullptr || sliceIndex >= (int) map->slices.size()) return;
    const int pos = map->slices[(size_t) sliceIndex].start;

    {
        const juce::ScopedLock sl (stateLock);
        auto it = std::min_element (manualPoints.begin(), manualPoints.end(),
                                    [pos] (int a, int b) { return std::abs (a - pos) < std::abs (b - pos); });
        if (it != manualPoints.end()) manualPoints.erase (it);
        if (sliceIndex < (int) sliceSettings.size())
            sliceSettings.erase (sliceSettings.begin() + sliceIndex);
    }
    rebuildSlices();
}

void SliceDeckProcessor::moveMarker (int sliceIndex, int newPos)
{
    if (sliceIndex < 1) return;
    convertToManual();
    auto map = getSliceMap();
    if (map == nullptr || sliceIndex >= (int) map->slices.size()) return;

    const auto& sl = map->slices;
    const int oldPos = sl[(size_t) sliceIndex].start;
    newPos = juce::jlimit (sl[(size_t) sliceIndex - 1].start + minSliceLen,
                           sl[(size_t) sliceIndex].end - minSliceLen, newPos);
    {
        const juce::ScopedLock lock (stateLock);
        auto it = std::min_element (manualPoints.begin(), manualPoints.end(),
                                    [oldPos] (int a, int b) { return std::abs (a - oldPos) < std::abs (b - oldPos); });
        if (it == manualPoints.end()) return;
        *it = newPos;
        std::sort (manualPoints.begin(), manualPoints.end());
    }
    rebuildSlices();
}

void SliceDeckProcessor::setSliceSetting (int index, SliceSetting setting)
{
    if (index < 0) return;
    {
        const juce::ScopedLock sl (stateLock);
        if ((int) sliceSettings.size() <= index) sliceSettings.resize ((size_t) index + 1);
        sliceSettings[(size_t) index] = setting;
    }
    auto old = getSliceMap();
    if (old == nullptr) return;
    auto m = std::make_shared<SliceMap> (*old);
    if (index < (int) m->slices.size())
    {
        m->slices[(size_t) index].gainDb = setting.gainDb;
        m->slices[(size_t) index].pitch = setting.pitch;
        m->slices[(size_t) index].loop = setting.loop;
    }
    const juce::SpinLock::ScopedLockType sl (dataLock);
    sliceMap = m;
}

SliceSetting SliceDeckProcessor::getSliceSetting (int index) const
{
    const juce::ScopedLock sl (stateLock);
    if (index >= 0 && index < (int) sliceSettings.size()) return sliceSettings[(size_t) index];
    return {};
}

int SliceDeckProcessor::getRootNote() const
{
    return (int) param ("mode") == modeSlice ? (int) param ("firstNote") : (int) param ("root");
}

void SliceDeckProcessor::previewNote (int note, bool on)
{
    if (note < 0 || note > 127) return;
    auto m = on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100)
                : juce::MidiMessage::noteOff (1, note);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    previewMidi.addMessageToQueue (m);
}

//==============================================================================
SliceDeckProcessor::Voice* SliceDeckProcessor::allocateVoice()
{
    for (auto& v : voices)
        if (! v.active) return &v;

    Voice* oldest = &voices[0];
    for (auto& v : voices)
        if (v.age < oldest->age) oldest = &v;
    return oldest;
}

void SliceDeckProcessor::stealVoice (Voice& v)
{
    if (v.active && v.stealRemaining == 0)
        v.stealRemaining = juce::jmax (1, (int) (hostRate * 0.005));
}

void SliceDeckProcessor::handleNoteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.note == note && ! v.ignoreNoteOff && ! v.releasing && v.stealRemaining == 0)
        {
            v.env.noteOff();
            v.releasing = true;
        }
}

void SliceDeckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    juce::MidiBuffer preview;
    previewMidi.removeNextBlockOfMessages (preview, numSamples);
    if (! preview.isEmpty()) midi.addEvents (preview, 0, numSamples, 0);

    std::shared_ptr<const SampleData> data;
    std::shared_ptr<const SliceMap> map;
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        data = sampleData;
        map = sliceMap;
    }

    if (data.get() != lastDataSeen)
    {
        for (auto& v : voices) v.active = false;   // new audio: old voice positions are meaningless
        lastDataSeen = data.get();
    }

    if (data == nullptr || map == nullptr || buffer.getNumChannels() < 2 || stretchers.empty())
    {
        playhead = -1;
        return;
    }

    // envelope settings
    juce::ADSR::Parameters envP;
    envP.attack  = param ("attack") * 0.001f;
    envP.decay   = param ("decay") * 0.001f;
    envP.sustain = param ("sustain") * 0.01f;
    envP.release = param ("release") * 0.001f;
    if (envP.attack != currentEnv.attack || envP.decay != currentEnv.decay
        || envP.sustain != currentEnv.sustain || envP.release != currentEnv.release)
    {
        currentEnv = envP;
        for (auto& v : voices) v.env.setParameters (envP);
    }

    // per-block settings shared by all voices
    BlockSettings bs;
    bs.transpose = param ("transpose") + param ("detune") * 0.01f;
    bs.speed = param ("speed") * 0.01f;
    bs.freeze = param ("freeze") > 0.5f;
    bs.freezePos = param ("freezePos") * 0.01f;
    bs.freezeSize = param ("freezeSize") * 0.001f;
    bs.character = (int) param ("character");
    bs.charAmount = param ("charAmount") * 0.01f;
    bs.rateRatio = data->sampleRate / hostRate;
    const double tempo = param ("sync") > 0.5f ? hostBpm.load() / juce::jmax (20.0f, param ("sampleBpm")) : 1.0;
    bs.stretchRate = juce::jlimit (0.0, 16.0, bs.rateRatio * tempo * std::abs (bs.speed));
    bs.formant = param ("formant");
    bs.keepFormants = param ("keepFormants") > 0.5f;

    const int mode = (int) param ("mode");
    const int engine = (int) param ("engine");
    const bool trigger = (int) param ("playback") == 0;
    const int trigBy = (int) param ("trigBy");
    const int maxVoices = (int) param ("voices");
    const float velAmt = param ("vel") * 0.01f;

    for (auto& v : voices)
        if (v.active && v.engine == engineStretch && ! v.frozen)
            applyStretchPitch (v, *data, bs);

    auto noteOn = [&] (int note, float velocity)
    {
        float semis = 0.0f;
        int start = map->trimStart, end = map->trimEnd;
        float gainDb = 0.0f;
        int sliceIdx = -1;
        bool sliceLoop = false;

        if (mode == modeSlice)
        {
            const int count = (int) map->slices.size();
            if (count == 0) return;
            if (trigBy == trigVelocity)    sliceIdx = juce::jlimit (0, count - 1, (int) (velocity * (float) count));
            else if (trigBy == trigSelect) sliceIdx = juce::jlimit (0, count - 1, (int) (param ("select") * 0.01f * (float) count));
            else                           sliceIdx = note - (int) param ("firstNote");
            if (sliceIdx < 0 || sliceIdx >= count) return;

            const auto& s = map->slices[(size_t) sliceIdx];
            start = s.start; end = s.end;
            semis += s.pitch;
            gainDb = s.gainDb;
            sliceLoop = s.loop;
            for (auto& v : voices)                     // choke the same slice
                if (v.active && (v.note == note || v.slice == sliceIdx)) stealVoice (v);
            lastSlice = sliceIdx;
        }
        else
        {
            semis += (float) (note - (int) param ("root"));
        }

        if (mode == modeOneShot)
        {
            for (auto& v : voices) stealVoice (v);
        }
        else
        {
            int playing = 0;
            for (auto& v : voices) if (v.active && v.stealRemaining == 0) ++playing;
            while (playing >= maxVoices)
            {
                Voice* oldest = nullptr;
                for (auto& v : voices)
                    if (v.active && v.stealRemaining == 0 && (oldest == nullptr || v.age < oldest->age))
                        oldest = &v;
                if (oldest == nullptr) break;
                stealVoice (*oldest);
                --playing;
            }
        }

        if (end - start < 4) return;
        Voice& v = *allocateVoice();
        v.active = true;
        v.releasing = false;
        v.stealRemaining = 0;
        v.note = note;
        v.slice = sliceIdx;
        v.noteSemis = semis;
        v.start = start;
        v.end = end;
        v.engine = engine;
        v.frozen = bs.freeze;
        v.loop = sliceLoop || ((mode == modeClassic) && param ("loop") > 0.5f);
        v.ignoreNoteOff = (mode != modeClassic) && trigger && ! v.loop;
        if (sliceLoop)
        {
            v.loopStart = start;
            v.fade = juce::jmin ((int) (0.005 * data->sampleRate), (end - start) / 4);
        }
        else
        {
            v.loopStart = start + (int) ((float) (end - start) * param ("loopStart") * 0.01f);
            if (v.loopStart >= end - 16) v.loopStart = start;
            const int fadeLen = (int) (param ("fade") * 0.001 * data->sampleRate);
            v.fade = juce::jmax (0, juce::jmin (fadeLen, v.loopStart, (end - v.loopStart) / 2));
        }
        v.inc = std::pow (2.0, (semis + bs.transpose) / 12.0) * bs.rateRatio * bs.speed;
        v.pos = v.inc >= 0.0 ? (double) start : (double) end - 1.0;
        v.gain = juce::Decibels::decibelsToGain (gainDb) * (1.0f - velAmt + velAmt * velocity);
        v.age = ++voiceCounter;
        v.lpL = v.lpR = v.holdL = v.holdR = 0.0f;
        v.holdPhase = 1.0f;

        const double center = start + bs.freezePos * (end - start);
        const double span = bs.freezeSize * hostRate * v.inc;
        v.grainPhase[0] = 0.0; v.grainPhase[1] = 0.5;
        v.grainAnchor[0] = v.grainAnchor[1] = center - span * 0.5;

        if (v.engine == engineStretch && ! v.frozen)
            startStretch (v, *data, bs);

        v.env.setSampleRate (hostRate);
        v.env.setParameters (currentEnv);
        v.env.reset();
        v.env.noteOn();
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, numSamples, meta.samplePosition);
        if (t > pos) { renderVoices (buffer, pos, t - pos, *data, bs); pos = t; }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())             noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())       handleNoteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (auto& v : voices) stealVoice (v);
    }
    if (pos < numSamples) renderVoices (buffer, pos, numSamples - pos, *data, bs);

    // playhead for the display
    const Voice* newest = nullptr;
    for (auto& v : voices)
        if (v.active && v.stealRemaining == 0 && (newest == nullptr || v.age > newest->age))
            newest = &v;
    if (newest == nullptr) playhead = -1;
    else if (newest->frozen) playhead = (int) (newest->start + bs.freezePos * (newest->end - newest->start));
    else playhead = (int) newest->pos;

    // filter
    if (param ("filterOn") > 0.5f)
    {
        const int type = (int) param ("filterType");
        filter.setType (type == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                      : type == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
                                  : juce::dsp::StateVariableTPTFilterType::bandpass);
        filter.setCutoffFrequency (juce::jmin (param ("cutoff"), (float) (hostRate * 0.45)));
        filter.setResonance (0.707f + param ("reso") * 0.01f * 9.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i) x[i] = filter.processSample (ch, x[i]);
        }
    }
    else
    {
        filter.reset();
    }

    // volume + pan
    outGain.setTargetValue (juce::Decibels::decibelsToGain (param ("volume"), -60.0f));
    const float pan = param ("pan") * 0.01f;
    const float pl = pan > 0.0f ? 1.0f - pan : 1.0f;
    const float pr = pan < 0.0f ? 1.0f + pan : 1.0f;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGain.getNextValue();
        L[i] *= g * pl;
        R[i] *= g * pr;
    }
}

//==============================================================================
namespace
{
    inline float readCubic (const float* s, int len, double p)
    {
        const int i = (int) std::floor (p);
        const float f = (float) (p - i);
        auto at = [s, len] (int k) { return s[juce::jlimit (0, len - 1, k)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
}

void SliceDeckProcessor::applyStretchPitch (Voice& v, const SampleData& data, const BlockSettings& bs)
{
    auto& st = *stretchers[(size_t) v.index];
    st.setTransposeFactor ((float) (std::pow (2.0, (v.noteSemis + bs.transpose) / 12.0) * data.sampleRate / hostRate));
    st.setFormantFactor ((float) std::pow (2.0, bs.formant / 12.0), bs.keepFormants);
}

// Reads n input samples for the stretcher from v.pos in direction dir (+1/-1),
// honouring loops. Fills silence after the end. Returns samples of real audio.
int SliceDeckProcessor::fetchInput (Voice& v, const SampleData& data, float* l, float* r, int n, int dir)
{
    const float* L = data.audio.getReadPointer (0);
    const float* R = data.audio.getReadPointer (1);
    const int len = data.length();
    int real = 0;
    for (int i = 0; i < n; ++i)
    {
        if (v.inputEnded) { l[i] = r[i] = 0.0f; continue; }
        const int p = juce::jlimit (0, len - 1, (int) v.pos);
        l[i] = L[p]; r[i] = R[p];
        ++real;
        v.pos += dir;
        if (dir > 0 && v.pos >= v.end)
        {
            if (v.loop && v.end > v.loopStart) v.pos = v.loopStart;
            else v.inputEnded = true;
        }
        else if (dir < 0 && v.pos < (v.loop ? v.loopStart : v.start))
        {
            if (v.loop && v.end > v.loopStart) v.pos = v.end - 1;
            else v.inputEnded = true;
        }
    }
    return real;
}

void SliceDeckProcessor::startStretch (Voice& v, const SampleData& data, const BlockSettings& bs)
{
    auto& st = *stretchers[(size_t) v.index];
    applyStretchPitch (v, data, bs);
    v.inputEnded = false;
    v.inFrac = 0.0;
    v.tailRemaining = 0;

    const int dir = bs.speed < 0.0f ? -1 : 1;
    const int seekLen = juce::jmin ((int) scratchL.size(), st.outputSeekLength ((float) juce::jmax (0.01, bs.stretchRate)));
    fetchInput (v, data, scratchL.data(), scratchR.data(), seekLen, dir);
    float* in[2] = { scratchL.data(), scratchR.data() };
    st.outputSeek (in, seekLen);
    if (v.inputEnded) v.tailRemaining = st.outputLatency() + st.inputLatency();
}

void SliceDeckProcessor::applyCharacter (Voice& v, float* l, float* r, int num, const BlockSettings& bs)
{
    const float a = bs.charAmount;
    if (bs.character == 1)          // Analog: soft saturation + gentle tape roll-off
    {
        const float drive = 1.0f + 4.0f * a;
        const float comp = 1.0f / std::sqrt (drive);
        const float cutoff = 18000.0f - 12000.0f * a;
        const float k = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) hostRate);
        for (int i = 0; i < num; ++i)
        {
            const float sl = std::tanh (drive * l[i] + 0.08f * a) - std::tanh (0.08f * a);
            const float sr = std::tanh (drive * r[i] + 0.08f * a) - std::tanh (0.08f * a);
            v.lpL += k * (sl - v.lpL);
            v.lpR += k * (sr - v.lpR);
            l[i] = v.lpL * comp;
            r[i] = v.lpR * comp;
        }
    }
    else if (bs.character == 2)     // Digital: bit and sample-rate reduction
    {
        const float bits = 16.0f - 12.0f * a;
        const float steps = std::pow (2.0f, bits - 1.0f);
        const float targetRate = 44100.0f / (1.0f + 15.0f * a * a);
        const float step = targetRate / (float) hostRate;
        for (int i = 0; i < num; ++i)
        {
            v.holdPhase += step;
            if (v.holdPhase >= 1.0f)
            {
                v.holdPhase -= std::floor (v.holdPhase);
                v.holdL = std::round (l[i] * steps) / steps;
                v.holdR = std::round (r[i] * steps) / steps;
            }
            l[i] = v.holdL;
            r[i] = v.holdR;
        }
    }
}

void SliceDeckProcessor::renderVoiceChunk (Voice& v, float* l, float* r, int num,
                                           const SampleData& data, const BlockSettings& bs)
{
    const float* srcL = data.audio.getReadPointer (0);
    const float* srcR = data.audio.getReadPointer (1);
    const int len = data.length();
    const float declick = (float) juce::jmax (1.0, data.sampleRate * 0.002);
    bool finished = false;

    if (v.frozen)
    {
        // two overlapping Hann grains around the freeze position
        const double inc = std::pow (2.0, (v.noteSemis + bs.transpose) / 12.0) * bs.rateRatio;
        const double grainOut = juce::jmax (8.0, bs.freezeSize * hostRate);
        const double dphi = 1.0 / grainOut;
        const double center = v.start + bs.freezePos * (v.end - v.start);
        for (int i = 0; i < num; ++i)
        {
            float sl = 0.0f, sr = 0.0f;
            for (int g = 0; g < 2; ++g)
            {
                const double phi = v.grainPhase[g];
                const float w = (float) std::pow (std::sin (juce::MathConstants<double>::pi * phi), 2.0);
                const double p = v.grainAnchor[g] + phi * grainOut * inc;
                sl += readCubic (srcL, len, p) * w;
                sr += readCubic (srcR, len, p) * w;
                v.grainPhase[g] += dphi;
                if (v.grainPhase[g] >= 1.0)
                {
                    v.grainPhase[g] -= 1.0;
                    // Restart near the freeze position, at the offset whose waveform best
                    // matches what the other grain is playing now (keeps the pitch steady)
                    const double target = center - grainOut * inc * 0.5;
                    const int other = 1 - g;
                    const double q = v.grainAnchor[other] + v.grainPhase[other] * grainOut * inc;
                    const int qi = (int) q;
                    const int win = 128;
                    const int range = (int) (0.012 * data.sampleRate);
                    const float* mono = data.mono.data();
                    const int ti = (int) target;
                    if (qi >= 0 && qi + win < len)
                    {
                        double best = -1.0e30; int bestS = ti;
                        for (int s = juce::jmax (0, ti - range); s <= juce::jmin (len - win - 1, ti + range); ++s)
                        {
                            double c = 0.0;
                            for (int k = 0; k < win; k += 2) c += (double) mono[s + k] * mono[qi + k];
                            if (c > best) { best = c; bestS = s; }
                        }
                        v.grainAnchor[g] = bestS + (q - qi);
                    }
                    else
                    {
                        v.grainAnchor[g] = target;
                    }
                }
            }
            l[i] = sl; r[i] = sr;
        }
    }
    else if (v.engine == engineStretch)
    {
        auto& st = *stretchers[(size_t) v.index];
        const int dir = bs.speed < 0.0f ? -1 : 1;
        const double want = num * bs.stretchRate + v.inFrac;
        int nIn = (int) want;
        v.inFrac = want - nIn;
        nIn = juce::jmin (nIn, (int) inL.size());
        fetchInput (v, data, inL.data(), inR.data(), nIn, dir);
        float* in[2] = { inL.data(), inR.data() };
        float* out[2] = { l, r };
        st.process (in, nIn, out, num);
        if (v.inputEnded)
        {
            if (v.tailRemaining == 0) v.tailRemaining = st.outputLatency() + st.inputLatency();
            v.tailRemaining -= num;
            if (v.tailRemaining <= 0) finished = true;
        }
    }
    else
    {
        // Repitch (speed can be negative)
        v.inc = std::pow (2.0, (v.noteSemis + bs.transpose) / 12.0) * bs.rateRatio * bs.speed;
        for (int i = 0; i < num; ++i)
        {
            if (finished) { l[i] = r[i] = 0.0f; continue; }
            const double p = v.pos;
            float sl = readCubic (srcL, len, p), sr = readCubic (srcR, len, p);

            if (v.loop && v.fade > 0 && v.inc > 0.0 && p > v.end - v.fade)
            {
                const float t = (float) ((p - (v.end - v.fade)) / v.fade);
                const double q = p - (v.end - v.loopStart);
                const float a = std::sqrt (1.0f - t), b = std::sqrt (t);
                sl = sl * a + readCubic (srcL, len, q) * b;
                sr = sr * a + readCubic (srcR, len, q) * b;
            }
            else if (v.loop && v.fade > 0 && v.inc < 0.0 && p < v.loopStart + v.fade)
            {
                const float t = (float) ((v.loopStart + v.fade - p) / v.fade);
                const double q = p + (v.end - v.loopStart);
                const float a = std::sqrt (1.0f - t), b = std::sqrt (t);
                sl = sl * a + readCubic (srcL, len, q) * b;
                sr = sr * a + readCubic (srcR, len, q) * b;
            }
            else if (! v.loop)
            {
                const float rem = (float) (v.inc >= 0.0 ? v.end - p : p - v.start);
                if (rem < declick) { const float g = juce::jmax (0.0f, rem / declick); sl *= g; sr *= g; }
            }
            l[i] = sl; r[i] = sr;

            v.pos += v.inc;
            if (v.inc >= 0.0 && v.pos >= v.end)
            {
                if (v.loop && v.end > v.loopStart) v.pos -= (v.end - v.loopStart);
                else finished = true;
            }
            else if (v.inc < 0.0 && v.pos < (v.loop ? v.loopStart : v.start))
            {
                if (v.loop && v.end > v.loopStart) v.pos += (v.end - v.loopStart);
                else finished = true;
            }
        }
    }

    if (finished) v.tailRemaining = -1;   // marker: stop after this chunk
}

void SliceDeckProcessor::renderVoices (juce::AudioBuffer<float>& out, int start, int num,
                                       const SampleData& data, const BlockSettings& bs)
{
    float* outL = out.getWritePointer (0);
    float* outR = out.getWritePointer (1);
    const int stealLen = juce::jmax (1, (int) (hostRate * 0.005));
    const int maxChunk = (int) voiceL.size();

    for (auto& v : voices)
    {
        if (! v.active) continue;

        for (int done = 0; done < num && v.active;)
        {
            const int n = juce::jmin (maxChunk, num - done);
            float* l = voiceL.data();
            float* r = voiceR.data();
            if (v.engine != engineStretch || v.frozen) v.tailRemaining = 0;
            renderVoiceChunk (v, l, r, n, data, bs);
            const bool endAfter = (v.engine != engineStretch || v.frozen) ? v.tailRemaining == -1
                                                                           : (v.inputEnded && v.tailRemaining <= 0);

            for (int i = 0; i < n; ++i) { l[i] *= v.gain; r[i] *= v.gain; }
            applyCharacter (v, l, r, n, bs);

            for (int i = 0; i < n; ++i)
            {
                float g = v.env.getNextSample();
                if (v.stealRemaining > 0)
                {
                    g *= (float) v.stealRemaining / (float) stealLen;
                    if (--v.stealRemaining == 0) { v.active = false; }
                }
                outL[start + done + i] += l[i] * g;
                outR[start + done + i] += r[i] * g;
                if (! v.active) break;
            }

            if (endAfter || (v.releasing && ! v.env.isActive()))
                v.active = false;
            if (! v.active) v.stealRemaining = 0;
            done += n;
        }
    }
}

//==============================================================================
juce::String SliceDeckProcessor::encodeAudio() const
{
    const juce::ScopedLock sl (stateLock);
    const int len = originalAudio.getNumSamples();
    if (len == 0) return {};

    auto write = [this, len] (juce::AudioFormat& format, int bits) -> juce::MemoryBlock
    {
        juce::MemoryBlock mb;
        auto* stream = new juce::MemoryOutputStream (mb, false);
        std::unique_ptr<juce::AudioFormatWriter> w (format.createWriterFor (stream, originalRate,
                                                    (unsigned int) originalAudio.getNumChannels(), bits, {}, 0));
        if (w == nullptr) { delete stream; return {}; }
        w->writeFromAudioSampleBuffer (originalAudio, 0, len);
        w.reset();   // flushes into mb
        return mb;
    };

    juce::MemoryBlock mb;
    if (originalAudio.getMagnitude (0, len) <= 1.0f)
    {
        juce::FlacAudioFormat flac;
        mb = write (flac, 24);
    }
    if (mb.isEmpty())
    {
        juce::WavAudioFormat wav;   // 32-bit float keeps peaks above 0 dBFS
        mb = write (wav, 32);
    }
    return mb.toBase64Encoding();
}

bool SliceDeckProcessor::decodeAudio (const juce::String& base64)
{
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (base64) || mb.isEmpty()) return false;

    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (mb, true)));
    if (r == nullptr || r->lengthInSamples <= 0) return false;

    const int len = (int) r->lengthInSamples;
    const int ch = (int) juce::jlimit (1u, 2u, r->numChannels);
    juce::AudioBuffer<float> buf (ch, len);
    r->read (&buf, 0, len, 0, true, ch > 1);

    const juce::ScopedLock sl (stateLock);
    originalAudio = std::move (buf);
    originalRate = r->sampleRate;
    return true;
}

bool SliceDeckProcessor::tryResolveMissing()
{
    juce::String rel, abs, name;
    {
        const juce::ScopedLock sl (stateLock);
        rel = relativePath; abs = absolutePath; name = sampleName;
    }

    juce::Array<juce::File> candidates;
    const auto folder = reaper.getProjectFolder();
    if (folder.isDirectory())
    {
        if (rel.isNotEmpty())  candidates.add (folder.getChildFile (rel));
        if (name.isNotEmpty()) candidates.add (folder.getChildFile (name));
    }
    if (abs.isNotEmpty() && juce::File::isAbsolutePath (abs))
        candidates.add (juce::File (abs));

    for (auto& f : candidates)
    {
        juce::AudioBuffer<float> buf;
        double rate = 44100.0;
        juce::String err;
        if (readAudioFile (formats, f, buf, rate, err))
        {
            const juce::ScopedLock sl (stateLock);
            originalAudio = std::move (buf);
            originalRate = rate;
            absolutePath = f.getFullPathName();
            missing = false;
            return true;
        }
    }
    return false;
}

void SliceDeckProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("SLICEDECK");
    root.setAttribute ("version", 1);
    if (auto xml = apvts.copyState().createXml())
        root.addChildElement (xml.release());

    const bool embed = param ("embed") > 0.5f;
    const juce::String audioText = embed ? encodeAudio() : juce::String();
    const auto folder = reaper.getProjectFolder();

    const juce::ScopedLock sl (stateLock);
    if (absolutePath.isNotEmpty() && folder.isDirectory() && juce::File (absolutePath).isAChildOf (folder))
        relativePath = juce::File (absolutePath).getRelativePathFrom (folder).replaceCharacter ('\\', '/');

    auto* s = root.createNewChildElement ("SAMPLE");
    s->setAttribute ("name", sampleName);
    s->setAttribute ("path", absolutePath);
    s->setAttribute ("relpath", relativePath);
    s->setAttribute ("reversedPoints", playbackReversed);
    if (audioText.isNotEmpty()) s->addTextElement (audioText);

    auto* sl2 = root.createNewChildElement ("SLICES");
    juce::StringArray pts, sets;
    for (int p : manualPoints) pts.add (juce::String (p));
    for (auto& st : sliceSettings) sets.add (juce::String (st.gainDb, 2) + ":" + juce::String (st.pitch, 2) + ":" + (st.loop ? "1" : "0"));
    sl2->setAttribute ("manual", pts.joinIntoString (","));
    sl2->setAttribute ("settings", sets.joinIntoString (","));

    copyXmlToBinary (root, destData);
}

void SliceDeckProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("SLICEDECK")) return;

    if (auto* p = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*p));

    {
        const juce::ScopedLock sl (stateLock);
        originalAudio.setSize (0, 0);
        sampleName.clear(); absolutePath.clear(); relativePath.clear();
        manualPoints.clear(); sliceSettings.clear();
        missing = false; missingRetries = 0;
        playbackReversed = false;

        if (auto* sl2 = xml->getChildByName ("SLICES"))
        {
            for (auto& t : juce::StringArray::fromTokens (sl2->getStringAttribute ("manual"), ",", ""))
                if (t.isNotEmpty()) manualPoints.push_back (t.getIntValue());
            for (auto& t : juce::StringArray::fromTokens (sl2->getStringAttribute ("settings"), ",", ""))
                if (t.isNotEmpty())
                {
                    auto parts = juce::StringArray::fromTokens (t, ":", "");
                    SliceSetting st;
                    st.gainDb = parts[0].getFloatValue();
                    st.pitch = parts[1].getFloatValue();
                    st.loop = parts[2].getIntValue() != 0;
                    sliceSettings.push_back (st);
                }
        }
    }

    if (auto* s = xml->getChildByName ("SAMPLE"))
    {
        {
            const juce::ScopedLock sl (stateLock);
            sampleName = s->getStringAttribute ("name");
            absolutePath = s->getStringAttribute ("path");
            relativePath = s->getStringAttribute ("relpath");
            playbackReversed = s->getBoolAttribute ("reversedPoints");
        }

        bool ok = false;
        const auto text = s->getAllSubText().trim();
        if (text.isNotEmpty()) ok = decodeAudio (text);
        if (! ok && (absolutePath.isNotEmpty() || relativePath.isNotEmpty()))
        {
            ok = tryResolveMissing();
            if (! ok)
            {
                const juce::ScopedLock sl (stateLock);
                missing = true;
            }
        }
    }

    needsReslice = false;
    needsPlaybackRebuild = false;
    rebuildPlayback();
}

juce::AudioProcessorEditor* SliceDeckProcessor::createEditor()
{
    return new SliceDeckEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SliceDeckProcessor();
}
