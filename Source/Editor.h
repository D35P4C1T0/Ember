#pragma once
#include "Processor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class EmberEditor final : public juce::AudioProcessorEditor, private juce::LookAndFeel_V4
{
public:
    explicit EmberEditor(EmberProcessor&);
    ~EmberEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    juce::Slider amount;
    juce::ComboBox focus;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> focusAttachment;
};
