#include "Editor.h"
#include <cmath>

EmberEditor::EmberEditor(EmberProcessor& p)
    : AudioProcessorEditor(p)
{
    setLookAndFeel(this);
    amount.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    amount.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 24);
    amount.setTextValueSuffix(" %");
    amount.setDoubleClickReturnValue(true, 0.0);
    amount.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffeee6d9));
    amount.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    amount.setTitle("Amount");
    focus.addItemList({"Low", "Full", "High"}, 1);
    focus.setJustificationType(juce::Justification::centred);
    focus.setTitle("Focus");
    focus.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff252b2b));
    focus.setColour(juce::ComboBox::textColourId, juce::Colour(0xffeee6d9));
    focus.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3b4543));
    addAndMakeVisible(amount);
    addAndMakeVisible(focus);
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters, "amount", amount);
    focusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.parameters, "mode", focus);
    setSize(420, 460);
}
EmberEditor::~EmberEditor() { setLookAndFeel(nullptr); }
void EmberEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff161d1d));
    g.setColour(juce::Colour(0xfff0e5d2));
    g.setFont(juce::FontOptions(30.0f));
    g.drawText("EMBER", 30, 25, 300, 40, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffa1ada7));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("HARMONIC COLOUR", 32, 67, 300, 20, juce::Justification::centredLeft);
    g.drawText("AMOUNT", 100, 106, 220, 20, juce::Justification::centred);
    g.drawText("FOCUS", 100, 344, 220, 20, juce::Justification::centred);
    g.setColour(juce::Colour(0xffe9a269));
    g.drawText("SATURATION / LOW / FULL / HIGH", 20, 425, 380, 20, juce::Justification::centred);
}
void EmberEditor::resized()
{
    amount.setBounds(90, 130, 240, 205);
    focus.setBounds(115, 373, 190, 32);
}
void EmberEditor::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                  float position, float start, float end, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(14.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = start + position * (end - start);
    juce::Path track, progress;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    progress.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
    g.setColour(juce::Colour(0xff35413e));
    g.strokePath(track, juce::PathStrokeType(4.0f));
    g.setColour(juce::Colour(0xffe9a269));
    g.strokePath(progress, juce::PathStrokeType(4.0f));
    g.setColour(juce::Colour(0xff252e2c));
    g.fillEllipse(centre.x - radius + 12, centre.y - radius + 12, 2 * radius - 24, 2 * radius - 24);
    const auto inner = radius * 0.55f;
    const auto outer = radius * 0.77f;
    g.setColour(juce::Colour(0xfff0e5d2));
    g.drawLine(centre.x + std::sin(angle) * inner, centre.y - std::cos(angle) * inner,
               centre.x + std::sin(angle) * outer, centre.y - std::cos(angle) * outer, 3.0f);
}
