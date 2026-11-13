#include "PluginEditor.h"

namespace Colours_
{
    const juce::Colour bg       (0xff18191d);
    const juce::Colour panel    (0xff23252b);
    const juce::Colour panelEdge(0xff30333a);
    const juce::Colour text     (0xffd9dade);
    const juce::Colour dim      (0xff8a8d96);
    const juce::Colour accent   (0xffff9e3b);
    const juce::Colour slice    (0xff4fd1c5);
    const juce::Colour wave     (0xffc9ccd4);
}
using namespace Colours_;

static juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); }

//==============================================================================
SliceDeckLook::SliceDeckLook()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, panelEdge);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
}

void SliceDeckLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                      float a0, float a1, juce::Slider&)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float rad = size * 0.5f;
    const float angle = a0 + pos * (a1 - a0);

    juce::Path track;
    track.addCentredArc (c.x, c.y, rad - 2.0f, rad - 2.0f, 0.0f, a0, a1, true);
    g.setColour (panelEdge.brighter (0.15f));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (c.x, c.y, rad - 2.0f, rad - 2.0f, 0.0f, a0, angle, true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (panelEdge);
    g.fillEllipse (r.reduced (6.0f));
    juce::Path tick;
    tick.addRoundedRectangle (-1.5f, -(rad - 7.0f), 3.0f, rad * 0.45f, 1.5f);
    g.setColour (text);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void SliceDeckLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto col = b.getToggleState() ? accent : panelEdge;
    if (over) col = col.brighter (0.1f);
    if (down) col = col.darker (0.1f);
    g.setColour (col);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
}

void SliceDeckLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (panelEdge);
    g.fillRoundedRectangle (r, 4.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (accent);
    g.fillPath (arrow);
}

//==============================================================================
float WaveformView::xFor (int sample, int len) const
{
    return len > 0 ? (float) getWidth() * (float) sample / (float) len : 0.0f;
}

int WaveformView::sampleAt (float x, int len) const
{
    return juce::jlimit (0, juce::jmax (0, len - 1), (int) ((double) x / juce::jmax (1, getWidth()) * len));
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
    const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();

    if (mode == SliceDeckProcessor::modeSlice)
    {
        idx = markerNear (x, map, len);
        if (idx > 0) return Drag::marker;
    }
    if (std::abs (x - xFor (map.trimStart, len)) < 6.0f) return Drag::trimStart;
    if (std::abs (x - xFor (map.trimEnd, len)) < 6.0f)   return Drag::trimEnd;

    if (mode == SliceDeckProcessor::modeClassic && proc.apvts.getRawParameterValue ("loop")->load() > 0.5f)
    {
        const float lp = proc.apvts.getRawParameterValue ("loopStart")->load() * 0.01f;
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
    auto bounds = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto data = proc.getSampleData();
    auto map = proc.getSliceMap();

    if (data == nullptr || map == nullptr || data->peakMin.empty())
    {
        g.setColour (proc.isSampleMissing() ? accent : dim);
        g.setFont (16.0f);
        g.drawText (proc.isSampleMissing() ? "Sample missing - drop the file here to relink"
                                           : "Drop an audio file here",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int len = data->length();
    const int w = getWidth();
    const float mid = (float) getHeight() * 0.5f;
    const float amp = (float) getHeight() * 0.46f;
    const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    const int bins = (int) data->peakMin.size();

    // slice backgrounds
    if (mode == SliceDeckProcessor::modeSlice)
    {
        for (int i = 0; i < (int) map->slices.size(); ++i)
        {
            const auto& s = map->slices[(size_t) i];
            const float x0 = xFor (s.start, len), x1 = xFor (s.end, len);
            if (i == selectedSlice)
            {
                g.setColour (slice.withAlpha (0.16f));
                g.fillRect (x0, 0.0f, x1 - x0, (float) getHeight());
            }
            else if (i % 2 == 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.025f));
                g.fillRect (x0, 0.0f, x1 - x0, (float) getHeight());
            }
        }
    }

    // waveform
    for (int x = 0; x < w; ++x)
    {
        const int b0 = (int) ((juce::int64) x * bins / w);
        const int b1 = juce::jmax (b0 + 1, (int) ((juce::int64) (x + 1) * bins / w));
        float mn = 0.0f, mx = 0.0f;
        for (int b = b0; b < juce::jmin (b1, bins); ++b)
        {
            mn = juce::jmin (mn, data->peakMin[(size_t) b]);
            mx = juce::jmax (mx, data->peakMax[(size_t) b]);
        }
        const int sample = (int) ((juce::int64) x * len / w);
        const bool inside = sample >= map->trimStart && sample < map->trimEnd;
        g.setColour (inside ? wave : wave.withAlpha (0.25f));
        g.drawVerticalLine (x, mid - juce::jmin (1.0f, mx) * amp, mid - juce::jmax (-1.0f, mn) * amp + 1.0f);
    }

    // slice markers
    if (mode == SliceDeckProcessor::modeSlice)
    {
        const int first = (int) proc.apvts.getRawParameterValue ("firstNote")->load();
        g.setFont (11.0f);
        for (int i = 0; i < (int) map->slices.size(); ++i)
        {
            const float x = xFor (map->slices[(size_t) i].start, len);
            g.setColour (slice.withAlpha (i == 0 ? 0.5f : 0.9f));
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
            const float x1 = xFor (map->slices[(size_t) i].end, len);
            if (x1 - x > 26.0f)
            {
                g.setColour (i == selectedSlice ? slice : dim);
                g.drawText (noteName (first + i), (int) x + 3, 3, 40, 14, juce::Justification::left);
            }
        }
    }

    // trim handles
    auto drawHandle = [&] (int sample, const juce::String& label, bool leftSide)
    {
        const float x = xFor (sample, len);
        g.setColour (accent);
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        juce::Path flag;
        const float fx = leftSide ? x : x - 14.0f;
        flag.addRectangle (fx, (float) getHeight() - 16.0f, 14.0f, 16.0f);
        g.fillPath (flag);
        g.setColour (juce::Colours::black);
        g.setFont (11.0f);
        g.drawText (label, (int) fx, getHeight() - 16, 14, 16, juce::Justification::centred);
    };
    drawHandle (map->trimStart, "S", true);
    drawHandle (map->trimEnd, "E", false);

    // loop start
    if (mode == SliceDeckProcessor::modeClassic && proc.apvts.getRawParameterValue ("loop")->load() > 0.5f)
    {
        const float lp = proc.apvts.getRawParameterValue ("loopStart")->load() * 0.01f;
        const int ls = map->trimStart + (int) ((float) (map->trimEnd - map->trimStart) * lp);
        const float x = xFor (ls, len);
        g.setColour (slice);
        const float dashes[] = { 4.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (x, 0.0f, x, (float) getHeight()), dashes, 2, 1.5f);
        g.setFont (11.0f);
        g.drawText ("LOOP", (int) x + 3, 3, 40, 14, juce::Justification::left);
    }

    // playhead
    const int ph = proc.getPlayheadSample();
    if (ph >= 0 && ph < len)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawVerticalLine ((int) xFor (ph, len), 0.0f, (float) getHeight());
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

    // click inside: select + audition
    const int sample = sampleAt ((float) e.x, len);
    const int mode = (int) proc.apvts.getRawParameterValue ("mode")->load();
    if (mode == SliceDeckProcessor::modeSlice)
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
    if ((int) proc.apvts.getRawParameterValue ("mode")->load() != SliceDeckProcessor::modeSlice) return;

    const int len = data->length();
    const int idx = markerNear ((float) e.x, *map, len);
    if (idx > 0) proc.removeMarker (idx);
    else         proc.addMarker (sampleAt ((float) e.x, len));
    repaint();
}

//==============================================================================
SliceDeckEditor::SliceDeckEditor (SliceDeckProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveform (p)
{
    setLookAndFeel (&look);

    addAndMakeVisible (waveform);
    waveform.onSliceSelected = [this] (int i) { selectSlice (i); };

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile()) loadFile (fc.getResult());
                              });
    };

    for (auto* l : { &nameLabel, &statusLabel, &sliceTitle })
        addAndMakeVisible (*l);
    nameLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    statusLabel.setColour (juce::Label::textColourId, dim);
    statusLabel.setFont (juce::FontOptions (12.5f));
    sliceTitle.setJustificationType (juce::Justification::centred);
    sliceTitle.setColour (juce::Label::textColourId, slice);

    setupCombo (mode, "mode", "Mode");
    setupCombo (playback, "playback", "Playback");
    setupCombo (sliceBy, "sliceBy", "Slice By");
    setupCombo (division, "division", "Division");
    setupCombo (filterType, "filterType", "Type");

    setupKnob (sensitivity, "sensitivity", "Sensitivity");
    setupKnob (regions, "regions", "Regions");
    setupKnob (voices, "voices", "Voices");
    setupKnob (root, "root", "Root");
    setupKnob (firstNote, "firstNote", "First Note");
    setupKnob (loopStart, "loopStart", "Loop Start");
    setupKnob (fade, "fade", "Loop Fade");
    for (auto* k : { &root, &firstNote })
    {
        k->slider.textFromValueFunction = [] (double v) { return noteName ((int) v); };
        k->slider.updateText();
    }

    setupKnob (attack, "attack", "Attack");
    setupKnob (decay, "decay", "Decay");
    setupKnob (sustain, "sustain", "Sustain");
    setupKnob (release, "release", "Release");
    setupKnob (vel, "vel", "Velocity");
    setupKnob (cutoff, "cutoff", "Cutoff");
    setupKnob (reso, "reso", "Resonance");
    setupKnob (volume, "volume", "Volume");
    setupKnob (pan, "pan", "Pan");
    setupKnob (transpose, "transpose", "Transpose");
    setupKnob (detune, "detune", "Detune");

    setupToggle (loop, "loop", "Loop");
    setupToggle (reverse, "reverse", "Reverse");
    setupToggle (snap, "snap", "Snap");
    setupToggle (filterOn, "filterOn", "Filter On");
    setupToggle (embed, "embed", "Embed in project");

    setupPlainKnob (sliceVol, "Slice Vol", -36.0, 12.0, 0.0, " dB");
    setupPlainKnob (slicePitch, "Slice Pitch", -24.0, 24.0, 0.0, " st");
    slicePitch.slider.setRange (-24.0, 24.0, 0.01);
    auto pushSlice = [this]
    {
        proc.setSliceSetting (waveform.selectedSlice, (float) sliceVol.slider.getValue(), (float) slicePitch.slider.getValue());
    };
    sliceVol.slider.onValueChange = pushSlice;
    slicePitch.slider.onValueChange = pushSlice;

    proc.addChangeListener (this);
    setSize (980, 640);
    changeListenerCallback (nullptr);
    selectSlice (0);
    updateVisibility();
    startTimerHz (30);
}

SliceDeckEditor::~SliceDeckEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void SliceDeckEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f));
    k.label.setColour (juce::Label::textColourId, dim);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);

    static const std::map<juce::String, juce::String> suffixes {
        { "attack", " ms" }, { "decay", " ms" }, { "release", " ms" }, { "fade", " ms" },
        { "sustain", " %" }, { "vel", " %" }, { "sensitivity", " %" }, { "reso", " %" }, { "loopStart", " %" },
        { "cutoff", " Hz" }, { "volume", " dB" }, { "detune", " ct" }, { "transpose", " st" } };
    auto it = suffixes.find (id);
    if (it != suffixes.end()) k.slider.setTextValueSuffix (it->second);
}

void SliceDeckEditor::setupPlainKnob (Knob& k, const juce::String& name, double min, double max, double def, const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setRange (min, max, 0.1);
    k.slider.setValue (def, juce::dontSendNotification);
    k.slider.setDoubleClickReturnValue (true, def);
    k.slider.setTextValueSuffix (suffix);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f));
    k.label.setColour (juce::Label::textColourId, dim);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

void SliceDeckEditor::setupCombo (Combo& c, const juce::String& id, const juce::String& name)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c.box.addItemList (p->choices, 1);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setFont (juce::FontOptions (12.0f));
    c.label.setColour (juce::Label::textColourId, dim);
    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<ComboAtt> (proc.apvts, id, c.box);
}

void SliceDeckEditor::setupToggle (Toggle& t, const juce::String& id, const juce::String& name)
{
    t.button.setButtonText (name);
    t.button.setClickingTogglesState (true);
    addAndMakeVisible (t.button);
    t.att = std::make_unique<ButtonAtt> (proc.apvts, id, t.button);
}

void SliceDeckEditor::placeKnob (Knob& k, juce::Rectangle<int> a)
{
    k.label.setBounds (a.removeFromTop (16));
    k.slider.setBounds (a);
}

void SliceDeckEditor::placeCombo (Combo& c, juce::Rectangle<int> a)
{
    c.label.setBounds (a.removeFromTop (16));
    c.box.setBounds (a.removeFromTop (24));
}

//==============================================================================
void SliceDeckEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SLICEDECK LITE", 16, 10, 200, 26, juce::Justification::left);

    auto drawBox = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, r.getX() + 10, r.getY() + 4, 200, 16, juce::Justification::left);
    };
    drawBox (sampleBox, "SAMPLE");
    drawBox (sliceBox, "SELECTED SLICE");
    drawBox (ampBox, "AMP ENVELOPE");
    drawBox (filterBox, "FILTER");
    drawBox (globalBox, "GLOBAL");
}

void SliceDeckEditor::resized()
{
    loadButton.setBounds (200, 11, 70, 26);
    nameLabel.setBounds (280, 11, 430, 26);
    embed.button.setBounds (getWidth() - 166, 11, 150, 26);
    statusLabel.setBounds (16, 42, getWidth() - 32, 20);
    waveform.setBounds (16, 66, getWidth() - 32, 220);

    sampleBox = { 16, 298, 718, 160 };
    sliceBox  = { 744, 298, 220, 160 };
    const int kw = 68, kh = 112;
    ampBox    = { 16, 468, kw * 5 + 20, 156 };
    filterBox = { ampBox.getRight() + 10, 468, 128 + kw * 2, 156 };
    globalBox = { filterBox.getRight() + 10, 468, getWidth() - 16 - (filterBox.getRight() + 10), 156 };

    // SAMPLE box
    {
        auto r = sampleBox.reduced (10).withTrimmedTop (16);
        auto combos = r.removeFromLeft (270);
        auto row1 = combos.removeFromTop (44);
        placeCombo (mode, row1.removeFromLeft (130));
        row1.removeFromLeft (10);
        placeCombo (sliceBy, row1.removeFromLeft (130));
        combos.removeFromTop (4);
        auto row2 = combos.removeFromTop (44);
        placeCombo (playback, row2.removeFromLeft (130));
        row2.removeFromLeft (10);
        placeCombo (division, row2.removeFromLeft (130));
        combos.removeFromTop (12);
        auto row3 = combos.removeFromTop (26);
        loop.button.setBounds (row3.removeFromLeft (80));
        row3.removeFromLeft (8);
        reverse.button.setBounds (row3.removeFromLeft (80));
        row3.removeFromLeft (8);
        snap.button.setBounds (row3.removeFromLeft (80));

        r.removeFromLeft (14);
        auto slot = [&] { auto s = r.removeFromLeft (kw).withHeight (kh); r.removeFromLeft (10); return s; };
        auto s1 = slot(); placeKnob (sensitivity, s1); placeKnob (regions, s1);
        auto s2 = slot(); placeKnob (voices, s2);
        auto s3 = slot(); placeKnob (root, s3); placeKnob (firstNote, s3);
        auto s4 = slot(); placeKnob (loopStart, s4);
        auto s5 = slot(); placeKnob (fade, s5);
    }

    // SLICE box
    {
        auto r = sliceBox.reduced (10).withTrimmedTop (16);
        sliceTitle.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);
        placeKnob (sliceVol, r.removeFromLeft (r.getWidth() / 2).withHeight (kh - 10));
        placeKnob (slicePitch, r.withHeight (kh - 10));
    }

    auto knobRow = [&] (juce::Rectangle<int> box, std::initializer_list<Knob*> knobs)
    {
        auto r = box.reduced (10).withTrimmedTop (18);
        for (auto* k : knobs) { placeKnob (*k, r.removeFromLeft (kw).withHeight (kh)); r.removeFromLeft (0); }
        return r;
    };
    knobRow (ampBox, { &attack, &decay, &sustain, &release, &vel });

    {
        auto r = filterBox.reduced (10).withTrimmedTop (18);
        auto left = r.removeFromLeft (100);
        filterOn.button.setBounds (left.removeFromTop (26));
        left.removeFromTop (10);
        placeCombo (filterType, left);
        r.removeFromLeft (8);
        placeKnob (cutoff, r.removeFromLeft (kw).withHeight (kh));
        placeKnob (reso, r.removeFromLeft (kw).withHeight (kh));
    }

    knobRow (globalBox, { &volume, &pan, &transpose, &detune });
}

//==============================================================================
void SliceDeckEditor::updateVisibility()
{
    const int m = (int) proc.apvts.getRawParameterValue ("mode")->load();
    const int by = (int) proc.apvts.getRawParameterValue ("sliceBy")->load();
    const bool lp = proc.apvts.getRawParameterValue ("loop")->load() > 0.5f;
    if (m == lastMode && by == lastSliceBy && lp == lastLoop) return;
    lastMode = m; lastSliceBy = by; lastLoop = lp;

    const bool isSlice = m == SliceDeckProcessor::modeSlice;
    const bool isClassic = m == SliceDeckProcessor::modeClassic;

    auto show = [] (bool v, std::initializer_list<juce::Component*> comps) { for (auto* c : comps) c->setVisible (v); };
    show (! isClassic, { &playback.box, &playback.label });
    show (isSlice, { &sliceBy.box, &sliceBy.label, &firstNote.slider, &firstNote.label,
                     &sliceVol.slider, &sliceVol.label, &slicePitch.slider, &slicePitch.label, &sliceTitle });
    show (! isSlice, { &root.slider, &root.label });
    show (isSlice && by == SliceDeckProcessor::byTransient, { &sensitivity.slider, &sensitivity.label });
    show (isSlice && by == SliceDeckProcessor::byRegion, { &regions.slider, &regions.label });
    show (isSlice && by == SliceDeckProcessor::byBeat, { &division.box, &division.label });
    show (m != SliceDeckProcessor::modeOneShot, { &voices.slider, &voices.label });
    show (isClassic, { &loop.button });
    show (isClassic && lp, { &loopStart.slider, &loopStart.label, &fade.slider, &fade.label });

    if (! isSlice) sliceTitle.setText ("Slice mode only", juce::dontSendNotification);
    else selectSlice (waveform.selectedSlice);
    waveform.repaint();
}

void SliceDeckEditor::selectSlice (int index)
{
    auto map = proc.getSliceMap();
    const int count = map != nullptr ? (int) map->slices.size() : 0;
    index = juce::jlimit (0, juce::jmax (0, count - 1), index);
    waveform.selectedSlice = index;

    const auto s = proc.getSliceSetting (index);
    sliceVol.slider.setValue (s.gainDb, juce::dontSendNotification);
    slicePitch.slider.setValue (s.pitch, juce::dontSendNotification);

    const int first = (int) proc.apvts.getRawParameterValue ("firstNote")->load();
    sliceTitle.setText (count > 0 ? "Slice " + juce::String (index + 1) + " of " + juce::String (count)
                                        + "  -  " + noteName (first + index)
                                  : "No slices",
                        juce::dontSendNotification);
}

void SliceDeckEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    nameLabel.setText (proc.getSampleName(), juce::dontSendNotification);
    statusLabel.setText (proc.getStatusText(), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, proc.isSampleMissing() ? accent : dim);
    if (lastMode == SliceDeckProcessor::modeSlice) selectSlice (waveform.selectedSlice);
    waveform.repaint();
}

void SliceDeckEditor::timerCallback()
{
    updateVisibility();
    waveform.repaint();

    if (++tickCount % 15 == 0)
    {
        statusLabel.setText (proc.getStatusText(), juce::dontSendNotification);
        nameLabel.setText (proc.getSampleName(), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, proc.isSampleMissing() ? accent : dim);
    }
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
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
        {
            loadFile (juce::File (f));
            return;
        }
}

void SliceDeckEditor::loadFile (const juce::File& f)
{
    juce::String error;
    if (! proc.loadFile (f, error))
    {
        statusLabel.setText ("Could not load " + f.getFileName() + ": " + error, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, accent);
        return;
    }
    selectSlice (0);
    changeListenerCallback (nullptr);
}
