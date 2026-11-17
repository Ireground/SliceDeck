#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

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
    bool fileHover = false;
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

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { waveform.fileHover = true; waveform.repaint(); }
    void fileDragExit (const juce::StringArray&) override { waveform.fileHover = false; waveform.repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    enum Kind { None, BtnLoad, BtnArrange, BtnEmbed, BtnReverse, BtnSnap, BtnLoop,
                BtnFreeze, BtnSync, BtnFormants, BtnSliceLoop, BtnFilterOn, Slider };
    struct Target { Kind kind = None; int index = -1; };

    struct SliderDef
    {
        juce::String id, label;
        juce::Colour colour;
        juce::Rectangle<float> rect;
        bool visible = false;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;

    void layoutSliders();
    Target findTarget (juce::Point<float>) const;
    juce::Rectangle<float> buttonRect (Kind) const;

    // slider helpers (parameters plus the two per-slice values)
    bool isSliceSlider (const SliderDef& d) const { return d.id == "sliceVol" || d.id == "slicePitch"; }
    float sliderNorm (const SliderDef&) const;
    void setSliderNorm (const SliderDef&, float norm);
    void nudgeSlider (const SliderDef&, float dir, bool fine);
    void resetSlider (const SliderDef&);
    juce::String sliderText (const SliderDef&) const;

    void drawPanel (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, juce::Colour dot) const;
    void drawPill (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, juce::Colour fill, juce::Colour textCol, float fontSize = 13.0f) const;
    void drawToggle (juce::Graphics&, Kind, const juce::String& text, bool on, juce::Colour onCol, float fontSize = 11.0f) const;
    void drawSlider (juce::Graphics&, const SliderDef&, int index) const;

    void loadFile (const juce::File&);
    void selectSlice (int index);

    SliceDeckProcessor& proc;
    WaveformView waveform;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<float> pTitle, pFile, pWave, pEngine, pChar, pFreeze, pStretch, pTrigger, pSlicing, pVoice, pEnv, pFilter, pOut, pSlice;
    juce::Rectangle<float> bLoad, bArrange, bEmbed, bReverse, bSnap, bLoop, bFreeze, bSync, bFormants, bSliceLoop, bFilterOn;
    std::vector<SliderDef> sliders;

    Target hover, active;
    float dragStartX = 0, dragStartNorm = 0;
    double messageTime = -100.0;
    juce::String message;
    void showMessage (const juce::String& m) { message = m.length() <= 10 ? m.toUpperCase() : juce::String ("FAILED"); messageTime = juce::Time::getMillisecondCounterHiRes() / 1000.0; repaint(); }
    int selectedSlice = 0;
    juce::String layoutKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceDeckEditor)
};
