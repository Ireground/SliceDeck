#include "PluginEditor.h"

namespace
{
    // HatsOff light scheme
    const juce::Colour cBg      { 0xFF141414 };
    const juce::Colour cPanel   { 0xFFE4E4E1 };
    const juce::Colour cInset   { 0xFFC9C9C5 };
    const juce::Colour cInset2  { 0xFFBBBBB7 };
    const juce::Colour cLabel   { 0xFF5C5C59 };
    const juce::Colour cText    { 0xFF1C1C1A };
    const juce::Colour cTrack   { 0xFFB4B4B0 };
    const juce::Colour cOrange  { 0xFFEF5C25 };
    const juce::Colour cBlue    { 0xFF3960B6 };
    const juce::Colour cTeal    { 0xFF2D908B };
    const juce::Colour cTealHov { 0xFF267C78 };
    const juce::Colour cPurple  { 0xFF8A4AB8 };
    const juce::Colour cPurpleH { 0xFF763CA0 };
    const juce::Colour cPurpleL { 0xFFCDB8E0 };
    const juce::Colour cRed     { 0xFFD23B2E };
    const juce::Colour cGreen   { 0xFF23955F };
    const juce::Colour cGreenL  { 0xFFC3E3D1 };
    const juce::Colour cGreenH  { 0xFF3DB97D };

    constexpr int W = 1080, H = 614;
    constexpr float G = 6.0f;

    juce::Font mono (float size, bool bold = false)
    {
       #if JUCE_WINDOWS
        const juce::String name ("Consolas");
       #else
        const juce::String name (juce::Font::getDefaultMonospacedFontName());
       #endif
        return juce::Font (juce::FontOptions (name, size, bold ? juce::Font::bold : juce::Font::plain));
    }

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    // Ableton-style names: 36 = C1, 60 = C3
    juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (juce::jlimit (0, 127, n), true, true, 3); }

    float raw (SliceDeckProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
}

//==============================================================================
//  Waveform
//==============================================================================
float WaveformView::xFor (int sample, int len) const
{
    return len > 0 ? 6.0f + (float) (getWidth() - 12) * (float) sample / (float) len : 0.0f;
}

int WaveformView::sampleAt (float x, int len) const
{
    return juce::jlimit (0, juce::jmax (0, len - 1), (int) ((double) (x - 6.0f) / juce::jmax (1, getWidth() - 12) * len));
}

int WaveformView::markerNear (float x, const SliceMap& map, int len) const
{
    for (int i = 1; i < (int) map.slices.size(); ++i)
        if (std::abs (x - xFor (map.slices[(size_t) i].start, len)) < 5.0f)
            return i;
    return -1;
}

WaveformView::Drag WaveformView::hitTestHandles (float x, const SliceMap& map, int len, int& idx) const
{
    idx = -1;
    const int mode = (int) raw (proc, "mode");
    if (mode == SliceDeckProcessor::modeSlice)
    {
        idx = markerNear (x, map, len);
        if (idx > 0) return Drag::marker;
    }
    if (std::abs (x - xFor (map.trimStart, len)) < 6.0f) return Drag::trimStart;
    if (std::abs (x - xFor (map.trimEnd, len)) < 6.0f)   return Drag::trimEnd;

    if (mode == SliceDeckProcessor::modeClassic && raw (proc, "loop") > 0.5f)
    {
        const float lp = raw (proc, "loopStart") * 0.01f;
        const int ls = map.trimStart + (int) ((float) (map.trimEnd - map.trimStart) * lp);
        if (std::abs (x - xFor (ls, len)) < 6.0f) return Drag::loopStart;
    }
    return Drag::none;
}

void WaveformView::setParam (const juce::String& id, float plain)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (cInset);
    g.fillRoundedRectangle (bounds, 5.0f);
    if (fileHover)
    {
        g.setColour (cTeal);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 5.0f, 2.0f);
    }

    auto data = proc.getSampleData();
    auto map = proc.getSliceMap();
    if (data == nullptr || map == nullptr || data->peakMin.empty())
    {
        g.setColour (proc.isSampleMissing() ? cRed : cLabel);
        g.setFont (mono (13.0f, true));
        g.drawText (proc.isSampleMissing() ? "SAMPLE MISSING - DROP THE FILE HERE TO RELINK" : "DROP AN AUDIO FILE HERE",
                    getLocalBounds().withTrimmedBottom (getHeight() / 2 - 2).withTrimmedTop (getHeight() / 2 - 20), juce::Justification::centred);
        g.setFont (mono (11.0f));
        g.drawText ("WAV  AIF  FLAC  MP3  OGG", getLocalBounds().withTrimmedTop (getHeight() / 2 + 2).withHeight (16), juce::Justification::centred);
        return;
    }

    const int len = data->length();
    const int w = getWidth();
    const float mid = (float) getHeight() * 0.5f + 4.0f;
    const float amp = (float) getHeight() * 0.40f;
    const int mode = (int) raw (proc, "mode");
    const int bins = (int) data->peakMin.size();
    const bool isSlice = mode == SliceDeckProcessor::modeSlice;

    // selected slice
    if (isSlice && selectedSlice < (int) map->slices.size())
    {
        const auto& s = map->slices[(size_t) selectedSlice];
        g.setColour (cGreenL);
        g.fillRect (xFor (s.start, len), 0.0f, xFor (s.end, len) - xFor (s.start, len), (float) getHeight());
    }

    // waveform
    for (int x = 6; x < w - 6; ++x)
    {
        const int b0 = (int) ((juce::int64) (x - 6) * bins / (w - 12));
        const int b1 = juce::jmax (b0 + 1, (int) ((juce::int64) (x - 5) * bins / (w - 12)));
        float mn = 0.0f, mx = 0.0f;
        for (int b = b0; b < juce::jmin (b1, bins); ++b)
        {
            mn = juce::jmin (mn, data->peakMin[(size_t) b]);
            mx = juce::jmax (mx, data->peakMax[(size_t) b]);
        }
        const int sample = sampleAt ((float) x, len);
        const bool inside = sample >= map->trimStart && sample < map->trimEnd;
        g.setColour (inside ? cText : cTrack);
        g.drawVerticalLine (x, mid - juce::jmin (1.0f, mx) * amp, mid - juce::jmax (-1.0f, mn) * amp + 1.0f);
    }

    // slice markers and note names
    if (isSlice)
    {
        const int first = (int) raw (proc, "firstNote");
        const int count = (int) map->slices.size();
        for (int i = 0; i < count; ++i)
        {
            const float x = xFor (map->slices[(size_t) i].start, len);
            const float x1 = xFor (map->slices[(size_t) i].end, len);
            g.setColour (cBlue.withAlpha (i == 0 ? 0.5f : 1.0f));
            g.fillRect (x, 0.0f, 1.5f, (float) getHeight());
            if (x1 - x > 24.0f)
            {
                g.setColour (i == selectedSlice ? cGreen : cBlue);
                g.setFont (mono (11.0f, true));
                g.drawText (noteName (first + i), (int) x + 4, 3, 40, 14, juce::Justification::left);
            }
        }
    }

    // freeze position
    if (raw (proc, "freeze") > 0.5f)
    {
        const float fp = raw (proc, "freezePos") * 0.01f;
        int fs = map->trimStart, fe = map->trimEnd;
        if (isSlice && selectedSlice < (int) map->slices.size())
        {
            fs = map->slices[(size_t) selectedSlice].start;
            fe = map->slices[(size_t) selectedSlice].end;
        }
        const float x = xFor (fs + (int) (fp * (float) (fe - fs)), len);
        g.setColour (cPurple);
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (x, 0.0f, x, (float) getHeight()), dashes, 2, 2.0f);
        g.setFont (mono (11.0f, true));
        g.drawText ("FREEZE", (int) x + 4, getHeight() - 34, 60, 14, juce::Justification::left);
    }

    // trim handles
    auto drawHandle = [&] (int sample, const char* label, bool leftSide)
    {
        const float x = xFor (sample, len);
        g.setColour (cOrange);
        g.fillRect (x - 0.75f, 0.0f, 1.5f, (float) getHeight());
        const float fx = leftSide ? x : x - 14.0f;
        g.fillRoundedRectangle (fx, (float) getHeight() - 17.0f, 14.0f, 17.0f, 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (mono (11.0f, true));
        g.drawText (label, (int) fx, getHeight() - 17, 14, 17, juce::Justification::centred);
    };
    drawHandle (map->trimStart, "S", true);
    drawHandle (map->trimEnd, "E", false);

    // loop start (Classic)
    if (mode == SliceDeckProcessor::modeClassic && raw (proc, "loop") > 0.5f)
    {
        const float lp = raw (proc, "loopStart") * 0.01f;
        const int ls = map->trimStart + (int) ((float) (map->trimEnd - map->trimStart) * lp);
        const float x = xFor (ls, len);
        g.setColour (cTeal);
        const float dashes[] = { 4.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (x, 0.0f, x, (float) getHeight()), dashes, 2, 1.5f);
        g.setFont (mono (11.0f, true));
        g.drawText ("LOOP", (int) x + 4, 3, 40, 14, juce::Justification::left);
    }

    // playhead
    const int ph = proc.getPlayheadSample();
    if (ph >= 0 && ph < len)
    {
        g.setColour (cGreen);
        g.fillRect (xFor (ph, len) - 1.0f, 0.0f, 2.0f, (float) getHeight());
    }
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    auto data = proc.getSampleData();
    auto map = proc.getSliceMap();
    if (data == nullptr || map == nullptr) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    int idx;
    const auto d = hitTestHandles ((float) e.x, *map, data->length(), idx);
    setMouseCursor (d != Drag::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    auto data = proc.getSampleData();
    auto map = proc.getSliceMap();
    if (data == nullptr || map == nullptr) return;
    const int len = data->length();

    drag = hitTestHandles ((float) e.x, *map, len, dragIndex);
    if (drag == Drag::marker && e.mods.isPopupMenu())
    {
        proc.removeMarker (dragIndex);
        drag = Drag::none;
        return;
    }

    const char* paramId = drag == Drag::trimStart ? "start"
                        : drag == Drag::trimEnd   ? "end"
                        : drag == Drag::loopStart ? "loopStart" : nullptr;
    if (paramId != nullptr)
    {
        gestureParam = proc.apvts.getParameter (paramId);
        if (gestureParam != nullptr) gestureParam->beginChangeGesture();
        return;
    }
    if (drag != Drag::none) return;

    const int sample = sampleAt ((float) e.x, len);
    if ((int) raw (proc, "mode") == SliceDeckProcessor::modeSlice)
    {
        for (int i = 0; i < (int) map->slices.size(); ++i)
            if (sample >= map->slices[(size_t) i].start && sample < map->slices[(size_t) i].end)
            {
                selectedSlice = i;
                if (onSliceSelected) onSliceSelected (i);
                previewing = proc.getRootNote() + i;
                break;
            }
    }
    else
    {
        previewing = proc.getRootNote();
    }
    if (previewing >= 0) proc.previewNote (previewing, true);
    repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    auto data = proc.getSampleData();
    auto map = proc.getSliceMap();
    if (data == nullptr || map == nullptr) return;
    const int len = data->length();
    const int sample = sampleAt ((float) e.x, len);

    switch (drag)
    {
        case Drag::trimStart: setParam ("start", (float) sample / (float) len); break;
        case Drag::trimEnd:   setParam ("end", (float) sample / (float) len); break;
        case Drag::marker:    proc.moveMarker (dragIndex, sample); break;
        case Drag::loopStart:
        {
            const int span = juce::jmax (1, map->trimEnd - map->trimStart);
            setParam ("loopStart", juce::jlimit (0.0f, 100.0f, 100.0f * (float) (sample - map->trimStart) / (float) span));
            break;
        }
        case Drag::none: break;
    }
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (gestureParam != nullptr) { gestureParam->endChangeGesture(); gestureParam = nullptr; }
    if (previewing >= 0) { proc.previewNote (previewing, false); previewing = -1; }
    drag = Drag::none;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto data = proc.getSampleData();
    auto map = proc.getSliceMap();
    if (data == nullptr || map == nullptr) return;
    if ((int) raw (proc, "mode") != SliceDeckProcessor::modeSlice) return;

    const int len = data->length();
    const int idx = markerNear ((float) e.x, *map, len);
    if (idx > 0) proc.removeMarker (idx);
    else         proc.addMarker (sampleAt ((float) e.x, len));
    repaint();
}

//==============================================================================
//  Editor
//==============================================================================
SliceDeckEditor::SliceDeckEditor (SliceDeckProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveform (p)
{
    addAndMakeVisible (waveform);
    waveform.onSliceSelected = [this] (int i) { selectSlice (i); };

    auto add = [this] (const char* id, const char* label, juce::Colour c) { sliders.push_back ({ id, label, c, {}, false }); };
    // SLICING
    add ("mode", "MODE", cBlue);          add ("sliceBy", "SLICE BY", cBlue);
    add ("sensitivity", "SENSITIVITY", cBlue); add ("division", "DIVISION", cBlue); add ("regions", "REGIONS", cBlue);
    add ("firstNote", "FIRST NOTE", cBlue); add ("root", "ROOT NOTE", cBlue);
    add ("loopStart", "LOOP START", cBlue); add ("fade", "LOOP FADE", cBlue);
    // VOICE
    add ("playback", "PLAYBACK", cTeal);  add ("voices", "VOICES", cTeal);
    add ("transpose", "TRANSPOSE", cTeal); add ("detune", "DETUNE", cTeal);
    // ENVELOPE
    add ("attack", "ATTACK", cOrange);    add ("decay", "DECAY", cOrange);
    add ("sustain", "SUSTAIN", cOrange);  add ("release", "RELEASE", cOrange);
    // FILTER
    add ("filterType", "TYPE", cPurple);  add ("cutoff", "CUTOFF", cPurple); add ("reso", "RESONANCE", cPurple);
    // OUTPUT
    add ("volume", "VOLUME", cGreen);     add ("pan", "PAN", cGreen);    add ("vel", "VELOCITY", cGreen);
    // SLICE
    add ("sliceVol", "VOLUME", cOrange);  add ("slicePitch", "PITCH", cOrange);
    // PLAYBACK
    add ("engine", "ENGINE", cBlue);      add ("speed", "SPEED", cBlue);
    add ("character", "CHARACTER", cOrange); add ("charAmount", "AMOUNT", cOrange);
    add ("freezePos", "POSITION", cPurple); add ("freezeSize", "GRAIN SIZE", cPurple);
    add ("sampleBpm", "SAMPLE BPM", cTeal); add ("formant", "FORMANT", cTeal);
    add ("trigBy", "TRIGGER BY", cGreen); add ("select", "SELECT", cGreen);

        proc.addChangeListener (this);
    setSize (W, H);
    startTimerHz (30);
}

SliceDeckEditor::~SliceDeckEditor()
{
    proc.removeChangeListener (this);
}

void SliceDeckEditor::resized()
{
    using R = juce::Rectangle<float>;
    const float y1 = G, h1 = 64.0f;
    const float y2 = y1 + h1 + G, h2 = 250.0f;
    const float y3 = y2 + h2 + G, h3 = 104.0f;
    const float y4 = y3 + h3 + G, h4 = H - y4 - G;

    // row 1
    const float tW = 230.0f;
    pTitle = R (G, y1, tW, h1);
    pFile  = R (pTitle.getRight() + G, y1, W - 3 * G - tW, h1);
    const float by = y1 + 28.0f, bh = 26.0f;
    bLoad    = R (pFile.getX() + 12, by, 74, bh);
    bArrange = R (bLoad.getRight() + 8, by, 150, bh);
    bEmbed   = R (bArrange.getRight() + 8, by, 128, bh);

    // row 2: waveform
    pWave = R (G, y2, W - 2 * G, h2);
    bLoop    = R (pWave.getRight() - 32 - 62, y2 + 7, 62, 17);
    bSnap    = R (bLoop.getX() - 6 - 62, y2 + 7, 62, 17);
    bReverse = R (bSnap.getX() - 6 - 78, y2 + 7, 78, 17);
    waveform.setBounds (R (pWave.getX() + 12, y2 + 30, pWave.getWidth() - 24, h2 - 42).toNearestInt());

    // row 3: playback, five panels
    {
        const float widths[] = { 190.0f, 190.0f, 200.0f, 270.0f, 0.0f };
        float x3 = G;
        int i3 = 0;
        for (auto* pnl : { &pEngine, &pChar, &pFreeze, &pStretch, &pTrigger })
        {
            const float w3 = pnl == &pTrigger ? W - G - x3 : widths[i3];
            *pnl = R (x3, y3, w3, h3);
            x3 += w3 + G;
            ++i3;
        }
        bFreeze   = R (pFreeze.getRight() - 32 - 44, y3 + 7, 44, 17);
        bSync     = R (pStretch.getRight() - 32 - 50, y3 + 7, 50, 17);
        bFormants = R (bSync.getX() - 6 - 110, y3 + 7, 110, 17);
    }

    // row 4: six panels
    const float pw = std::floor ((W - 7 * G) / 6.0f);
    float x = G;
    for (auto* pnl : { &pSlicing, &pVoice, &pEnv, &pFilter, &pOut, &pSlice })
    {
        *pnl = R (x, y4, pnl == &pSlice ? W - G - x : pw, h4);
        x += pw + G;
    }
    bFilterOn  = R (pFilter.getRight() - 32 - 44, y4 + 7, 44, 17);
    bSliceLoop = R (pSlice.getRight() - 32 - 46, y4 + 7, 46, 17);

    layoutKey.clear();
    layoutSliders();
}

// Which sliders are visible depends on the mode, so the layout is refreshed whenever that changes.
void SliceDeckEditor::layoutSliders()
{
    const int mode = (int) raw (proc, "mode");
    const int by = (int) raw (proc, "sliceBy");
    const bool loop = raw (proc, "loop") > 0.5f;
    auto sm = proc.getSliceMap();
    const bool hasSlices = sm != nullptr && ! sm->slices.empty();
    const int eng = (int) raw (proc, "engine");
    const int chr = (int) raw (proc, "character");
    const int trg = (int) raw (proc, "trigBy");
    const auto key = juce::String (mode) + "/" + juce::String (by) + "/" + juce::String ((int) loop) + "/" + juce::String ((int) hasSlices)
                   + "/" + juce::String (eng) + "/" + juce::String (chr) + "/" + juce::String (trg);
    if (key == layoutKey) return;
    layoutKey = key;

    const bool isSlice = mode == SliceDeckProcessor::modeSlice;
    const bool isClassic = mode == SliceDeckProcessor::modeClassic;

    std::vector<juce::String> slicing { "mode" };
    if (isSlice)
    {
        slicing.push_back ("sliceBy");
        if (by == SliceDeckProcessor::byTransient) slicing.push_back ("sensitivity");
        if (by == SliceDeckProcessor::byBeat)      slicing.push_back ("division");
        if (by == SliceDeckProcessor::byRegion)    slicing.push_back ("regions");
        slicing.push_back ("firstNote");
    }
    else
    {
        slicing.push_back ("root");
        if (isClassic && loop) { slicing.push_back ("loopStart"); slicing.push_back ("fade"); }
    }
    std::vector<juce::String> voice;
    if (! isClassic) voice.push_back ("playback");
    if (mode != SliceDeckProcessor::modeOneShot) voice.push_back ("voices");
    voice.push_back ("transpose"); voice.push_back ("detune");

    const std::vector<std::pair<juce::Rectangle<float>, std::vector<juce::String>>> panels {
        { pSlicing, slicing },
        { pVoice, voice },
        { pEnv, { "attack", "decay", "sustain", "release" } },
        { pFilter, { "filterType", "cutoff", "reso" } },
        { pOut, { "volume", "pan", "vel" } },
        { pSlice, isSlice && hasSlices ? std::vector<juce::String> { "sliceVol", "slicePitch" } : std::vector<juce::String>() },
        { pEngine, { "engine", "speed" } },
        { pChar, chr != 0 ? std::vector<juce::String> { "character", "charAmount" } : std::vector<juce::String> { "character" } },
        { pFreeze, { "freezePos", "freezeSize" } },
        { pStretch, eng == SliceDeckProcessor::engineStretch ? std::vector<juce::String> { "sampleBpm", "formant" } : std::vector<juce::String>() },
        { pTrigger, ! isSlice ? std::vector<juce::String>()
                              : trg == SliceDeckProcessor::trigSelect ? std::vector<juce::String> { "trigBy", "select" }
                                                                      : std::vector<juce::String> { "trigBy" } } };

    for (auto& s : sliders) s.visible = false;
    for (auto& [panel, ids] : panels)
        for (size_t row = 0; row < ids.size(); ++row)
            for (auto& s : sliders)
                if (s.id == ids[row])
                {
                    s.visible = true;
                    s.rect = { panel.getX() + 14, panel.getY() + 30 + (float) row * 32.0f, panel.getWidth() - 28, 30 };
                }

}

void SliceDeckEditor::timerCallback()
{
    layoutSliders();
    repaint();
    waveform.repaint();
}

//==============================================================================
float SliceDeckEditor::sliderNorm (const SliderDef& d) const
{
    if (d.id == "sliceVol")   return juce::jmap (proc.getSliceSetting (selectedSlice).gainDb, -36.0f, 12.0f, 0.0f, 1.0f);
    if (d.id == "slicePitch") return juce::jmap (proc.getSliceSetting (selectedSlice).pitch, -24.0f, 24.0f, 0.0f, 1.0f);
    if (auto* p = proc.apvts.getParameter (d.id)) return p->getValue();
    return 0.0f;
}

void SliceDeckEditor::setSliderNorm (const SliderDef& d, float norm)
{
    norm = juce::jlimit (0.0f, 1.0f, norm);
    if (isSliceSlider (d))
    {
        auto s = proc.getSliceSetting (selectedSlice);
        if (d.id == "sliceVol") s.gainDb = std::round (juce::jmap (norm, -36.0f, 12.0f) * 10.0f) / 10.0f;
        else                    s.pitch  = std::round (juce::jmap (norm, -24.0f, 24.0f) * 10.0f) / 10.0f;
        proc.setSliceSetting (selectedSlice, s);
        return;
    }
    if (auto* p = proc.apvts.getParameter (d.id))
    {
        const float snapped = p->convertTo0to1 (p->convertFrom0to1 (norm));
        if (std::abs (snapped - p->getValue()) > 1.0e-6f) p->setValueNotifyingHost (snapped);
    }
}

void SliceDeckEditor::nudgeSlider (const SliderDef& d, float dir, bool fine)
{
    if (isSliceSlider (d))
    {
        auto s = proc.getSliceSetting (selectedSlice);
        if (d.id == "sliceVol") s.gainDb = juce::jlimit (-36.0f, 12.0f, s.gainDb + dir * (fine ? 0.1f : 0.5f));
        else                    s.pitch  = juce::jlimit (-24.0f, 24.0f, s.pitch + dir * (fine ? 0.1f : 1.0f));
        proc.setSliceSetting (selectedSlice, s);
        return;
    }
    auto* p = proc.apvts.getParameter (d.id);
    if (p == nullptr) return;
    const auto& range = p->getNormalisableRange();
    const bool stepped = dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr || dynamic_cast<juce::AudioParameterInt*> (p) != nullptr;
    if (stepped)
    {
        const float v = range.convertFrom0to1 (p->getValue()) + dir;
        p->setValueNotifyingHost (range.convertTo0to1 (juce::jlimit (range.start, range.end, v)));
    }
    else
    {
        setSliderNorm (d, p->getValue() + dir * (fine ? 0.0025f : 0.01f));
    }
}

void SliceDeckEditor::resetSlider (const SliderDef& d)
{
    if (isSliceSlider (d))
    {
        auto s = proc.getSliceSetting (selectedSlice);
        if (d.id == "sliceVol") s.gainDb = 0.0f; else s.pitch = 0.0f;
        proc.setSliceSetting (selectedSlice, s);
        return;
    }
    if (auto* p = proc.apvts.getParameter (d.id))
        p->setValueNotifyingHost (p->getDefaultValue());
}

juce::String SliceDeckEditor::sliderText (const SliderDef& d) const
{
    auto sign = [] (float v, int dec) { return (v > 0.0f ? "+" : "") + juce::String (v, dec); };
    if (d.id == "sliceVol")   return sign (proc.getSliceSetting (selectedSlice).gainDb, 1) + " dB";
    if (d.id == "slicePitch") return sign (proc.getSliceSetting (selectedSlice).pitch, 1) + " st";

    auto* p = proc.apvts.getParameter (d.id);
    if (p == nullptr) return {};
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p)) return c->getCurrentChoiceName().toUpperCase();
    const float v = p->convertFrom0to1 (p->getValue());
    const juce::String id = d.id;

    if (id == "firstNote" || id == "root") return noteName ((int) v);
    if (id == "voices" || id == "regions") return juce::String ((int) v);
    if (id == "freezeSize") return juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (id == "sampleBpm") return juce::String (v, 1);
    if (id == "formant") return sign (v, 1) + " st";
    if (id == "speed") return juce::String ((int) std::round (v)) + "%";
    if (id == "attack" || id == "decay" || id == "release" || id == "fade")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
    if (id == "cutoff") return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String ((int) v) + " Hz";
    if (id == "volume") return sign (v, 1) + " dB";
    if (id == "pan") return std::abs (v) < 0.5f ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String ((int) std::round (std::abs (v)));
    if (id == "transpose") return sign ((float) (int) v, 0) + " st";
    if (id == "detune") return sign (v, 0) + " ct";
    return juce::String ((int) std::round (v)) + "%";
}

//==============================================================================
void SliceDeckEditor::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour dot) const
{
    g.setColour (cPanel);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (cLabel);
    g.setFont (mono (12.0f));
    g.drawText (title, r.getX() + 12, r.getY() + 8, r.getWidth() - 40, 14, juce::Justification::centredLeft);
    g.setColour (dot);
    g.fillEllipse (r.getRight() - 19, r.getY() + 11, 8, 8);
}

void SliceDeckEditor::drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                                juce::Colour fill, juce::Colour textCol, float fontSize) const
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
    g.setColour (textCol);
    g.setFont (mono (fontSize, true));
    g.drawText (text, r, juce::Justification::centred);
}

void SliceDeckEditor::drawToggle (juce::Graphics& g, Kind k, const juce::String& text, bool on, juce::Colour onCol, float fontSize) const
{
    const bool hov = hover.kind == k && active.kind == None;
    drawPill (g, buttonRect (k), text, on ? (hov ? onCol.darker (0.15f) : onCol) : (hov ? cInset2 : cInset),
              on ? juce::Colours::white : cLabel, fontSize);
}

void SliceDeckEditor::drawSlider (juce::Graphics& g, const SliderDef& d, int index) const
{
    const auto r = d.rect;
    g.setFont (mono (12.0f));
    g.setColour (cLabel);
    g.drawText (d.label, r.getX(), r.getY(), r.getWidth() * 0.6f, 15, juce::Justification::centredLeft);
    g.setFont (mono (13.0f, true));
    g.setColour (cText);
    g.drawText (sliderText (d), r.getX() + r.getWidth() * 0.3f, r.getY(), r.getWidth() * 0.7f, 15, juce::Justification::centredRight);

    const float t = sliderNorm (d);
    float t0 = 0.0f;
    if (d.id == "pan" || d.id == "transpose" || d.id == "detune" || d.id == "slicePitch" || d.id == "speed" || d.id == "formant") t0 = 0.5f;
    if (d.id == "sliceVol") t0 = 0.75f;
    g.setColour (cTrack);
    g.fillRect (r.getX(), r.getY() + 21, r.getWidth(), 2.0f);
    g.setColour (d.colour);
    g.fillRect (r.getX() + std::min (t, t0) * r.getWidth(), r.getY() + 21, std::abs (t - t0) * r.getWidth(), 2.0f);
    const bool isActive = active.kind == Slider && active.index == index;
    g.setColour (isActive ? cText : d.colour);
    g.fillRect (r.getX() + t * r.getWidth() - 1.5f, r.getY() + 16, 3.0f, 12.0f);
}

void SliceDeckEditor::paint (juce::Graphics& g)
{
    g.fillAll (cBg);
    const bool embed = raw (proc, "embed") > 0.5f;
    const int mode = (int) raw (proc, "mode");
    const bool isSlice = mode == SliceDeckProcessor::modeSlice;
    auto data = proc.getSampleData();
    auto map = proc.getSliceMap();
    const bool hasSmp = data != nullptr;
    const int numSlices = map != nullptr ? (int) map->slices.size() : 0;
    auto hov = [this] (Kind k) { return hover.kind == k && active.kind == None; };

    // ---- title ----
    g.setColour (cPanel);
    g.fillRoundedRectangle (pTitle, 7.0f);
    g.setFont (mono (25.0f, true));
    g.setColour (cText);
    g.drawText ("SLICE", pTitle.getX() + 14, pTitle.getY() + 9, 80, 28, juce::Justification::centredLeft);
    g.setColour (cOrange);
    g.drawText ("DECK", pTitle.getX() + 14 + 69, pTitle.getY() + 9, 70, 28, juce::Justification::centredLeft);
    g.setFont (mono (11.0f));
    g.setColour (cLabel);
    g.drawText ("SLICE SAMPLER", pTitle.getX() + 15, pTitle.getY() + 41, pTitle.getWidth() - 20, 14, juce::Justification::centredLeft);

    juce::String st; juce::Colour stc;
    if (nowSeconds() - messageTime < 4.0)                             { st = message;        stc = cRed; }
    else if (proc.isSampleMissing())                                  { st = "FILE MISSING"; stc = cRed; }
    else if (! hasSmp)                                                { st = "NO SAMPLE";    stc = cOrange; }
    else if (raw (proc, "freeze") > 0.5f)                             { st = "FROZEN";       stc = cPurple; }
    else                                                              { st = "READY";        stc = cLabel; }
    {
        const float w = st.length() * 6.4f + 18.0f;
        drawPill (g, { pTitle.getRight() - 12 - w, pTitle.getY() + 10, w, 18 }, st, stc, juce::Colours::white, 11.0f);
    }

    // ---- sample file ----
    drawPanel (g, pFile, "SAMPLE FILE", cTeal);
    drawPill (g, bLoad, "LOAD", hov (BtnLoad) ? cTealHov : cTeal, juce::Colours::white);
    drawPill (g, bArrange, "FROM ARRANGE", ! proc.isInReaper() ? cInset : (hov (BtnArrange) ? cTealHov : cTeal),
              proc.isInReaper() ? juce::Colours::white : cLabel, 12.0f);
    drawToggle (g, BtnEmbed, embed ? "EMBEDDED" : "EMBED IN PROJECT", embed, cTeal, 11.0f);
    {
        const auto area = juce::Rectangle<float> (bEmbed.getRight() + 12, bEmbed.getY(), pFile.getRight() - 12 - bEmbed.getRight() - 12, bEmbed.getHeight());
        g.setFont (mono (13.0f, true));
        g.setColour (proc.isSampleMissing() ? cRed : cText);
        auto name = proc.getSampleName();
        if (name.isEmpty()) name = "-";
        g.drawText (name, area.withHeight (14), juce::Justification::centredLeft, true);
        g.setFont (mono (11.0f));
        g.setColour (cLabel);
        g.drawText (proc.getStatusText().toUpperCase(), area.withTrimmedTop (14), juce::Justification::centredLeft, true);
    }

    // ---- waveform ----
    {
        juce::String title = "SAMPLE";
        if (hasSmp && isSlice) title << "  /  " << numSlices << " SLICES";
        if (hasSmp && isSlice) title << "    CLICK = PLAY   DOUBLE-CLICK = ADD/REMOVE MARKER   DRAG MARKERS + S/E";
        drawPanel (g, pWave, title, cBlue);
        drawToggle (g, BtnReverse, "REVERSE", raw (proc, "reverse") > 0.5f, cBlue);
        drawToggle (g, BtnSnap, "SNAP", raw (proc, "snap") > 0.5f, cBlue);
        if (mode == SliceDeckProcessor::modeClassic)
            drawToggle (g, BtnLoop, "LOOP", raw (proc, "loop") > 0.5f, cBlue);
    }

    // ---- playback ----
    {
        const bool stretch = (int) raw (proc, "engine") == SliceDeckProcessor::engineStretch;
        drawPanel (g, pEngine, "ENGINE", cBlue);
        drawPanel (g, pChar, "CHARACTER", cOrange);
        drawPanel (g, pFreeze, "FREEZE", cPurple);
        drawToggle (g, BtnFreeze, "ON", raw (proc, "freeze") > 0.5f, cPurple);
        drawPanel (g, pStretch, "STRETCH", cTeal);
        g.setFont (mono (11.0f));
        g.setColour (cLabel);
        if (stretch)
        {
            drawToggle (g, BtnSync, "SYNC", raw (proc, "sync") > 0.5f, cTeal);
            drawToggle (g, BtnFormants, "KEEP FORMANTS", raw (proc, "keepFormants") > 0.5f, cTeal);
        }
        else
            g.drawText ("STRETCH ENGINE ONLY", pStretch.withTrimmedTop (30).reduced (12, 0).withHeight (14), juce::Justification::centredLeft);
        drawPanel (g, pTrigger, "TRIGGER SLICES", cGreen);
        if (! isSlice)
        {
            g.setColour (cLabel);
            g.drawText ("SLICE MODE ONLY", pTrigger.withTrimmedTop (30).reduced (12, 0).withHeight (14), juce::Justification::centredLeft);
        }
    }

    // ---- bottom panels ----
    drawPanel (g, pSlicing, "SLICING", cBlue);
    drawPanel (g, pVoice, "VOICE", cTeal);
    drawPanel (g, pEnv, "ENVELOPE", cOrange);
    drawPanel (g, pFilter, "FILTER", cPurple);
    drawToggle (g, BtnFilterOn, "ON", raw (proc, "filterOn") > 0.5f, cPurple);
    drawPanel (g, pOut, "OUTPUT", cGreen);
    if (isSlice && numSlices > 0)
    {
        drawPanel (g, pSlice, "SLICE " + juce::String (selectedSlice + 1) + "/" + juce::String (numSlices), cOrange);
        drawToggle (g, BtnSliceLoop, "LOOP", proc.getSliceSetting (selectedSlice).loop, cOrange);
    }
    else
    {
        drawPanel (g, pSlice, "SLICE", cOrange);
        g.setColour (cLabel);
        g.setFont (mono (11.0f));
        g.drawText (isSlice ? "NO SLICES YET" : "SLICE MODE ONLY", pSlice.withTrimmedTop (30).reduced (12, 0).withHeight (14),
                    juce::Justification::centredLeft);
    }

    for (int i = 0; i < (int) sliders.size(); ++i)
        if (sliders[(size_t) i].visible) drawSlider (g, sliders[(size_t) i], i);
}

//==============================================================================
juce::Rectangle<float> SliceDeckEditor::buttonRect (Kind k) const
{
    switch (k)
    {
        case BtnLoad: return bLoad;         case BtnEmbed: return bEmbed;     case BtnArrange: return bArrange;
        case BtnReverse: return bReverse;   case BtnSnap: return bSnap;
        case BtnLoop: return (int) raw (proc, "mode") == SliceDeckProcessor::modeClassic ? bLoop : juce::Rectangle<float>();
        case BtnFreeze: return bFreeze;
        case BtnSync: case BtnFormants:
            if ((int) raw (proc, "engine") != SliceDeckProcessor::engineStretch) return {};
            return k == BtnSync ? bSync : bFormants;
        case BtnSliceLoop:
        {
            auto m = proc.getSliceMap();
            return (int) raw (proc, "mode") == SliceDeckProcessor::modeSlice && m != nullptr && ! m->slices.empty() ? bSliceLoop : juce::Rectangle<float>();
        }
        case BtnFilterOn: return bFilterOn;
        case None: case Slider: default: return {};
    }
}

SliceDeckEditor::Target SliceDeckEditor::findTarget (juce::Point<float> p) const
{
    for (auto k : { BtnLoad, BtnArrange, BtnEmbed, BtnReverse, BtnSnap, BtnLoop, BtnFreeze, BtnSync, BtnFormants, BtnSliceLoop, BtnFilterOn })
        if (buttonRect (k).contains (p)) return { k, -1 };
    for (int i = 0; i < (int) sliders.size(); ++i)
        if (sliders[(size_t) i].visible && sliders[(size_t) i].rect.expanded (0, 2).contains (p)) return { Slider, i };
    return {};
}

void SliceDeckEditor::mouseMove (const juce::MouseEvent& e)
{
    hover = findTarget (e.position);
    repaint();
}

void SliceDeckEditor::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void SliceDeckEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto t = findTarget (e.position);
    active = t;
    auto toggle = [this] (const char* id) { proc.setParamValue (id, raw (proc, id) > 0.5f ? 0.0f : 1.0f); };

    switch (t.kind)
    {
        case BtnLoad:
            chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
            break;
        case BtnArrange:
        {
            juce::String err;
            if (proc.importFromArrange (err)) selectSlice (0);
            else showMessage (err);
            break;
        }
        case BtnEmbed:     toggle ("embed"); break;
        case BtnReverse:   toggle ("reverse"); break;
        case BtnSnap:      toggle ("snap"); break;
        case BtnLoop:      toggle ("loop"); break;
        case BtnFreeze:    toggle ("freeze"); break;
        case BtnSync:      toggle ("sync"); break;
        case BtnFormants:  toggle ("keepFormants"); break;
        case BtnFilterOn:  toggle ("filterOn"); break;
        case BtnSliceLoop:
        {
            auto st = proc.getSliceSetting (selectedSlice);
            st.loop = ! st.loop;
            proc.setSliceSetting (selectedSlice, st);
            break;
        }
        case Slider:
        {
            const auto& d = sliders[(size_t) t.index];
            dragStartX = e.position.x;
            dragStartNorm = sliderNorm (d);
            if (! isSliceSlider (d))
                if (auto* p = proc.apvts.getParameter (d.id)) p->beginChangeGesture();
            break;
        }
        case None: default: break;
    }
    repaint();
}

void SliceDeckEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (active.kind == Slider)
    {
        const auto& d = sliders[(size_t) active.index];
        const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
        setSliderNorm (d, dragStartNorm + (e.position.x - dragStartX) / d.rect.getWidth() * fine);
    }
    repaint();
}

void SliceDeckEditor::mouseUp (const juce::MouseEvent& e)
{
    if (active.kind == Slider)
    {
        const auto& d = sliders[(size_t) active.index];
        if (! isSliceSlider (d))
            if (auto* p = proc.apvts.getParameter (d.id)) p->endChangeGesture();
    }
    active = {};
    hover = findTarget (e.position);
    repaint();
}

void SliceDeckEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = findTarget (e.position);
    if (t.kind == Slider) resetSlider (sliders[(size_t) t.index]);
    active = {};
    repaint();
}

void SliceDeckEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-4f) return;
    const auto t = findTarget (e.position);
    if (t.kind == Slider)
        nudgeSlider (sliders[(size_t) t.index], w.deltaY > 0 ? 1.0f : -1.0f, e.mods.isShiftDown());
    repaint();
}

//==============================================================================
void SliceDeckEditor::selectSlice (int index)
{
    auto map = proc.getSliceMap();
    const int count = map != nullptr ? (int) map->slices.size() : 0;
    selectedSlice = juce::jlimit (0, juce::jmax (0, count - 1), index);
    waveform.selectedSlice = selectedSlice;
    repaint();
}

bool SliceDeckEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;
    return false;
}

void SliceDeckEditor::filesDropped (const juce::StringArray& files, int, int)
{
    waveform.fileHover = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
        {
            loadFile (juce::File (f));
            break;
        }
    repaint();
}

void SliceDeckEditor::loadFile (const juce::File& f)
{
    juce::String error;
    if (! proc.loadFile (f, error))
    {
        showMessage ("LOAD FAILED");
        return;
    }
    selectSlice (0);
    repaint();
}
