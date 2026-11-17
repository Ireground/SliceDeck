#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SliceEngine.h"

namespace
{
    const double divisionBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    constexpr int maxSlices = 128;
    constexpr int minSliceLen = 16;

    bool readAudioFile (juce::AudioFormatManager& fm, const juce::File& f,
                        juce::AudioBuffer<float>& dest, double& rate, juce::String& error,
                        double startSec = 0.0, double lengthSec = 0.0)
    {
        if (! f.existsAsFile()) { error = "File not found"; return false; }
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) { error = "Unsupported audio format"; return false; }
        if (r->lengthInSamples <= 0 || r->sampleRate <= 0) { error = "File is empty"; return false; }

        const juce::int64 total = r->lengthInSamples;
        const juce::int64 first = juce::jlimit ((juce::int64) 0, total - 1, (juce::int64) std::llround (startSec * r->sampleRate));
        juce::int64 count = lengthSec > 0.0 ? (juce::int64) std::llround (lengthSec * r->sampleRate) : total - first;
        count = juce::jmin (count, total - first);
        if (count < 64) { error = "Clip is too short"; return false; }
        if ((double) count / r->sampleRate > 600.0) { error = "Longer than 10 minutes"; return false; }

        const int len = (int) count;
        const int ch = (int) juce::jlimit (1u, 2u, r->numChannels);
        dest.setSize (ch, len);
        r->read (&dest, 0, len, first, true, ch > 1);
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

    // Random Play
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "rndOn", 2 }, "Random Play", false));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { "rndCount", 2 }, "Random Slices", 1, 64, 7));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "rndRate", 2 }, "Random Rate", StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dragLen", 2 }, "Drag Length", StringArray { "LOOP", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 0));
    return l;
}

SliceDeckProcessor::SliceDeckProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();

    for (auto* id : { "mode", "sliceBy", "sensitivity", "division", "regions", "start", "end", "snap" })
        apvts.addParameterListener (id, this);
    apvts.addParameterListener ("reverse", this);

    lastRndCount = (int) param ("rndCount");
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

    // new slice count for Random Play -> pick a new set
    if ((int) param ("rndCount") != lastRndCount)
    {
        randomize();
        lastRndCount = (int) param ("rndCount");
    }
    if (rndLen.load() == 0)
        if (auto m = getSliceMap(); m != nullptr && ! m->slices.empty())
            randomize();

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
bool SliceDeckProcessor::importFromArrange (juce::String& error)
{
    ReaperBridge::ArrangeItem item;
    if (! reaper.getSelectedAudioItem (item, error)) return false;
    {
        const juce::ScopedLock sl (stateLock);
        missing = false;     // an import is always a new sample, never a relink
    }
    return loadFile (item.file, error, item.startSec, item.lengthSec, item.name);
}

bool SliceDeckProcessor::loadFile (const juce::File& file, juce::String& error,
                                   double startSec, double lengthSec, const juce::String& displayName)
{
    juce::AudioBuffer<float> buf;
    double rate = 44100.0;
    if (! readAudioFile (formats, file, buf, rate, error, startSec, lengthSec))
        return false;

    bool relink = false;
    {
        const juce::ScopedLock sl (stateLock);
        relink = missing;      // relinking keeps slice markers and settings
        originalAudio = std::move (buf);
        originalRate = rate;
        sampleName = displayName.isNotEmpty() ? displayName : file.getFileName();
        absolutePath = file.getFullPathName();
        rangeStart = startSec;
        rangeLength = lengthSec;
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
    }

    needsReslice = false;
    rebuildPlayback();
    if (! relink) randomize();
    return true;
}

//==============================================================================
double SliceDeckProcessor::stepBeatsFor (int r)
{
    static const double beats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return beats[juce::jlimit (0, 5, r)];
}

void SliceDeckProcessor::randomize()
{
    auto map = getSliceMap();
    const int count = juce::jlimit (1, MaxRandom, (int) param ("rndCount"));
    const int numSlices = map != nullptr ? (int) map->slices.size() : 0;
    if (numSlices == 0) { rndLen = 0; sendChangeMessage(); return; }

    // distinct slices while there are enough, then repeats; random order
    std::vector<int> pool;
    std::vector<int> out;
    while ((int) out.size() < count)
    {
        if (pool.empty())
        {
            for (int i = 0; i < numSlices; ++i) pool.push_back (i);
            for (int i = numSlices - 1; i > 0; --i) std::swap (pool[(size_t) i], pool[(size_t) rng.nextInt (i + 1)]);
        }
        out.push_back (pool.back());
        pool.pop_back();
    }
    for (int i = 0; i < count; ++i) rndSeq[(size_t) i] = out[(size_t) i];
    rndLen = count;
    sendChangeMessage();
}

juce::File SliceDeckProcessor::writeMidiFile()
{
    constexpr int tpq = 960;
    const int len = rndLen.load();
    auto map = getSliceMap();
    if (len == 0 || map == nullptr || map->slices.empty()) return {};

    const double stl = stepBeatsFor ((int) param ("rndRate"));
    const double L = len * stl;
    const int dl = juce::jlimit (0, 4, (int) param ("dragLen"));
    static const int bars[] = { 0, 1, 2, 4, 8 };
    const double total = dl == 0 ? L : bars[dl] * 4.0;
    const int first = (int) param ("firstNote");

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "SliceDeck Lite"), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    for (int k = 0; k * stl < total - 1.0e-9; ++k)
    {
        const int slice = rndSeq[(size_t) (k % len)].load() % (int) map->slices.size();
        const int note = juce::jlimit (0, 127, first + slice);
        const double on = std::round (k * stl * tpq);
        const double off = std::round (std::min (total, (k + 1) * stl) * tpq);
        seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), on);
        seq.addEvent (juce::MidiMessage::noteOff (1, note), std::max (on + 1.0, off - 1.0));
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), std::round (total * tpq));
    seq.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (tpq);
    file.addTrack (seq);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SliceDeck Lite");
    dir.createDirectory();
    const juce::String lenTxt = dl == 0 ? juce::String (len) + "slices" : juce::String (bars[dl]) + "bar";
    auto out = dir.getChildFile (juce::File::createLegalFileName (
        "SliceDeck Lite " + lenTxt + " " + juce::Time::getCurrentTime().formatted ("%H%M%S") + ".mid"));
    juce::FileOutputStream os (out);
    if (! os.openedOk()) return {};
    os.setPosition (0);
    os.truncate();
    file.writeTo (os);
    os.flush();
    return out;
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

void SliceDeckProcessor::setSliceSetting (int index, float gainDb, float pitch)
{
    if (index < 0) return;
    {
        const juce::ScopedLock sl (stateLock);
        if ((int) sliceSettings.size() <= index) sliceSettings.resize ((size_t) index + 1);
        sliceSettings[(size_t) index] = { gainDb, pitch };
    }
    auto old = getSliceMap();
    if (old == nullptr) return;
    auto m = std::make_shared<SliceMap> (*old);
    if (index < (int) m->slices.size())
    {
        m->slices[(size_t) index].gainDb = gainDb;
        m->slices[(size_t) index].pitch = pitch;
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

    if (data == nullptr || map == nullptr || buffer.getNumChannels() < 2)
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

    const int mode = (int) param ("mode");
    const bool trigger = (int) param ("playback") == 0;
    const int maxVoices = (int) param ("voices");
    const float velAmt = param ("vel") * 0.01f;
    const float transpose = param ("transpose") + param ("detune") * 0.01f;
    const double rateRatio = data->sampleRate / hostRate;

    auto noteOn = [&] (int note, float velocity, int forcedSlice)
    {
        float semis = transpose;
        int start = map->trimStart, end = map->trimEnd;
        float gainDb = 0.0f;
        int sliceIdx = -1;

        if (mode == modeSlice)
        {
            sliceIdx = forcedSlice >= 0 ? forcedSlice : note - (int) param ("firstNote");
            if (sliceIdx < 0 || sliceIdx >= (int) map->slices.size()) return;
            const auto& s = map->slices[(size_t) sliceIdx];
            start = s.start; end = s.end;
            semis += s.pitch;
            gainDb = s.gainDb;
            for (auto& v : voices) if (v.active && v.note == note) stealVoice (v);   // choke same slice
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
        v.start = start;
        v.end = end;
        v.pos = (double) start;
        v.inc = std::pow (2.0, semis / 12.0) * rateRatio;
        v.ignoreNoteOff = ((mode != modeClassic) && trigger) || note == RandomNote;
        v.loop = (mode == modeClassic) && param ("loop") > 0.5f;
        v.loopStart = start + (int) ((float) (end - start) * param ("loopStart") * 0.01f);
        if (v.loopStart >= end - 16) v.loopStart = start;
        const int fadeLen = (int) (param ("fade") * 0.001 * data->sampleRate);
        v.fade = juce::jmax (0, juce::jmin (fadeLen, v.loopStart, (end - v.loopStart) / 2));
        v.gain = juce::Decibels::decibelsToGain (gainDb) * (1.0f - velAmt + velAmt * velocity);
        v.age = ++voiceCounter;
        v.env.setSampleRate (hostRate);
        v.env.setParameters (currentEnv);
        v.env.reset();
        v.env.noteOn();
    };

    // ---- Random Play: one random slice per step while the transport runs ----
    struct GenEvent { int offset; int slice; };
    std::array<GenEvent, 128> gen {};
    int numGen = 0;
    {
        double bpm = 120.0, ppq = 0.0;
        bool hostPlaying = false;
        if (auto* ph = getPlayHead())
            if (auto p = ph->getPosition())
            {
                hostPlaying = p->getIsPlaying();
                if (auto q = p->getPpqPosition()) ppq = *q; else hostPlaying = false;
                if (auto b = p->getBpm()) bpm = *b;
            }

        const int len = rndLen.load();
        const bool active = hostPlaying && param ("rndOn") > 0.5f && mode == modeSlice
                            && len > 0 && ! map->slices.empty();
        const double spb = hostRate * 60.0 / juce::jmax (1.0, bpm);
        const double stl = stepBeatsFor ((int) param ("rndRate"));
        const double b0 = (wasPlaying && std::abs (ppq - prevB1) < 2.0 / spb) ? prevB1 : ppq;
        const double b1 = b0 + numSamples / spb;
        prevB1 = b1;

        if (active)
        {
            for (double k = std::ceil (b0 / stl - 1.0e-9); k * stl < b1 && numGen < (int) gen.size(); k += 1.0)
            {
                const double t = k * stl;
                if (t < b0 - 1.0e-9) continue;
                const long long step = (long long) std::llround (k);
                const int idx = (int) (((step % len) + len) % len);
                gen[(size_t) numGen++] = { juce::jlimit (0, numSamples - 1, (int) std::floor ((t - b0) * spb + 0.5)),
                                           getRandomSlice (idx) % (int) map->slices.size() };
            }
            const long long cur = (long long) std::floor (b0 / stl + 1.0e-9);
            rndStep = (int) (((cur % len) + len) % len);
        }
        else
        {
            if (wasPlaying)
                for (auto& v : voices) if (v.active && v.note == RandomNote) stealVoice (v);
            rndStep = -1;
        }
        wasPlaying = active;
        transportPlaying = hostPlaying;
    }

    int pos = 0, gi = 0;
    auto handleGen = [&] (int upTo)
    {
        while (gi < numGen && gen[(size_t) gi].offset <= upTo)
        {
            const int t = gen[(size_t) gi].offset;
            if (t > pos) { renderVoices (buffer, pos, t - pos, *data, *map); pos = t; }
            for (auto& v : voices) if (v.active && v.note == RandomNote) stealVoice (v);
            noteOn (RandomNote, 0.8f, gen[(size_t) gi].slice);
            ++gi;
        }
    };

    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, numSamples, meta.samplePosition);
        handleGen (t - 1);
        if (t > pos) { renderVoices (buffer, pos, t - pos, *data, *map); pos = t; }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())             noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), -1);
        else if (msg.isNoteOff())       handleNoteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (auto& v : voices) stealVoice (v);
    }
    handleGen (numSamples);
    if (pos < numSamples) renderVoices (buffer, pos, numSamples - pos, *data, *map);

    // playhead for the display
    const Voice* newest = nullptr;
    for (auto& v : voices)
        if (v.active && v.stealRemaining == 0 && (newest == nullptr || v.age > newest->age))
            newest = &v;
    playhead = newest != nullptr ? (int) newest->pos : -1;

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

void SliceDeckProcessor::renderVoices (juce::AudioBuffer<float>& out, int start, int num,
                                       const SampleData& data, const SliceMap&)
{
    const float* srcL = data.audio.getReadPointer (0);
    const float* srcR = data.audio.getReadPointer (1);
    const int len = data.length();
    float* outL = out.getWritePointer (0);
    float* outR = out.getWritePointer (1);
    const float declick = (float) juce::jmax (1.0, data.sampleRate * 0.002);
    const int stealLen = juce::jmax (1, (int) (hostRate * 0.005));

    auto read = [len] (const float* s, double p) -> float
    {
        const int i = (int) p;
        const float f = (float) (p - i);
        auto at = [s, len] (int k) { return s[juce::jlimit (0, len - 1, k)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    };

    for (auto& v : voices)
    {
        if (! v.active) continue;

        for (int i = start; i < start + num; ++i)
        {
            float g = v.gain;
            bool finish = false;

            if (v.stealRemaining > 0)
            {
                g *= (float) v.stealRemaining / (float) stealLen;
                if (--v.stealRemaining == 0) finish = true;
            }

            const float e = v.env.getNextSample();
            const double p = v.pos;
            float l = read (srcL, p), r = read (srcR, p);

            if (v.loop && v.fade > 0 && p > v.end - v.fade)
            {
                const float t = (float) ((p - (v.end - v.fade)) / v.fade);
                const double q = p - (v.end - v.loopStart);
                const float a = std::sqrt (1.0f - t), b = std::sqrt (t);
                l = l * a + read (srcL, q) * b;
                r = r * a + read (srcR, q) * b;
            }
            else if (! v.loop)
            {
                const float rem = (float) (v.end - p);
                if (rem < declick) g *= juce::jmax (0.0f, rem / declick);
            }

            outL[i] += l * g * e;
            outR[i] += r * g * e;

            v.pos += v.inc;
            if (v.pos >= v.end)
            {
                if (v.loop && v.end > v.loopStart) v.pos -= (v.end - v.loopStart);
                else finish = true;
            }
            if (v.releasing && ! v.env.isActive()) finish = true;

            if (finish) { v.active = false; v.stealRemaining = 0; break; }
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
    double rs = 0.0, rl = 0.0;
    {
        const juce::ScopedLock sl (stateLock);
        rel = relativePath; abs = absolutePath; name = absolutePath.isNotEmpty() ? juce::File (absolutePath).getFileName() : sampleName;
        rs = rangeStart; rl = rangeLength;
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
        if (readAudioFile (formats, f, buf, rate, err, rs, rl))
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
    s->setAttribute ("rangeStart", rangeStart);
    s->setAttribute ("rangeLength", rangeLength);
    if (audioText.isNotEmpty()) s->addTextElement (audioText);

    auto* sl2 = root.createNewChildElement ("SLICES");
    juce::StringArray pts, sets;
    for (int p : manualPoints) pts.add (juce::String (p));
    for (auto& st : sliceSettings) sets.add (juce::String (st.gainDb, 2) + ":" + juce::String (st.pitch, 2));
    sl2->setAttribute ("manual", pts.joinIntoString (","));
    sl2->setAttribute ("settings", sets.joinIntoString (","));

    auto* rnd = root.createNewChildElement ("RANDOM");
    juce::StringArray seqTxt;
    for (int i = 0; i < rndLen.load(); ++i) seqTxt.add (juce::String (rndSeq[(size_t) i].load()));
    rnd->setAttribute ("seq", seqTxt.joinIntoString (","));

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
        sampleName.clear(); absolutePath.clear(); relativePath.clear(); rangeStart = rangeLength = 0.0;
        manualPoints.clear(); sliceSettings.clear();
        missing = false; missingRetries = 0;
        playbackReversed = false;

        if (auto* rnd = xml->getChildByName ("RANDOM"))
        {
            int n = 0;
            for (auto& t : juce::StringArray::fromTokens (rnd->getStringAttribute ("seq"), ",", ""))
                if (t.isNotEmpty() && n < MaxRandom) rndSeq[(size_t) n++] = t.getIntValue();
            rndLen = n;
        }
        lastRndCount = (int) param ("rndCount");

        if (auto* sl2 = xml->getChildByName ("SLICES"))
        {
            for (auto& t : juce::StringArray::fromTokens (sl2->getStringAttribute ("manual"), ",", ""))
                if (t.isNotEmpty()) manualPoints.push_back (t.getIntValue());
            for (auto& t : juce::StringArray::fromTokens (sl2->getStringAttribute ("settings"), ",", ""))
                if (t.isNotEmpty())
                    sliceSettings.push_back ({ t.upToFirstOccurrenceOf (":", false, false).getFloatValue(),
                                               t.fromFirstOccurrenceOf (":", false, false).getFloatValue() });
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
            rangeStart = s->getDoubleAttribute ("rangeStart");
            rangeLength = s->getDoubleAttribute ("rangeLength");
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
