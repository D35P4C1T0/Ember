#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FloatEngine.h"
#include <limits>

class EmberProcessor final : public juce::AudioProcessor
{
public:
    EmberProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ember"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return std::numeric_limits<double>::infinity(); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorValueTreeState parameters;
private:
    void processAudio(juce::AudioBuffer<float>&, juce::MidiBuffer&, float bypass);
    ember::FloatEngine engine;
    std::atomic<float>* amountValue = nullptr;
    std::atomic<float>* modeValue = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EmberProcessor)
};
