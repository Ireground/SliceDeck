#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class SliceDeckLook : public juce::LookAndFeel_V4
{
public:
    SliceDeckLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
};

//==============================================================================
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (SliceDeckProcessor& p) : proc (p) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    int selectedSlice = 0;
    std::function<void (int)> onSliceSelected;

private:
    enum class Drag { none, trimStart, trimEnd, marker, loopStart };

    float xFor (int sample, int len) const;
    int sampleAt (float x, int len) const;
    int markerNear (float x, const SliceMap&, int len) const;
    Drag hitTestHandles (float x, const SliceMap&, int len, int& markerIndex) const;
    void setParam (const juce::String& id, float plain);

    SliceDeckProcessor& proc;
    Drag drag = Drag::none;
    int dragIndex = -1, previewing = -1;
    juce::RangedAudioParameter* gestureParam = nullptr;
};

//==============================================================================
class SliceDeckEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit SliceDeckEditor (SliceDeckProcessor&);
    ~SliceDeckEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob  { juce::Slider slider; juce::Label label; std::unique_ptr<SliderAtt> att; };
    struct Combo { juce::ComboBox box; juce::Label label; std::unique_ptr<ComboAtt> att; };
    struct Toggle { juce::TextButton button; std::unique_ptr<ButtonAtt> att; };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void setupCombo (Combo&, const juce::String& paramId, const juce::String& name);
    void setupToggle (Toggle&, const juce::String& paramId, const juce::String& name);
    void setupPlainKnob (Knob&, const juce::String& name, double min, double max, double def, const juce::String& suffix);
    void placeKnob (Knob&, juce::Rectangle<int> area);
    void placeCombo (Combo&, juce::Rectangle<int> area);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateVisibility();
    void selectSlice (int index);
    void loadFile (const juce::File&);

    SliceDeckProcessor& proc;
    SliceDeckLook look;
    WaveformView waveform;

    juce::TextButton loadButton { "Load" };
    juce::Label nameLabel, statusLabel, sliceTitle;
    std::unique_ptr<juce::FileChooser> chooser;

    Combo mode, playback, sliceBy, division, filterType;
    Knob sensitivity, regions, voices, root, firstNote, loopStart, fade;
    Knob attack, decay, sustain, release, vel;
    Knob cutoff, reso;
    Knob volume, pan, transpose, detune;
    Knob sliceVol, slicePitch;
    Toggle loop, reverse, snap, filterOn, embed;

    juce::Rectangle<int> sampleBox, sliceBox, ampBox, filterBox, globalBox;
    int lastMode = -1, lastSliceBy = -1;
    bool lastLoop = false;
    int tickCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceDeckEditor)
};
