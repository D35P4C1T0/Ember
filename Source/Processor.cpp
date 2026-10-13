#include "Processor.h"
#include "Editor.h"

EmberProcessor::EmberProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "EmberState", {
          std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"amount", 1}, "Amount", juce::NormalisableRange<float>{0.0f, 1.0f}, 0.0f,
              juce::AudioParameterFloatAttributes().withLabel("%")
                  .withStringFromValueFunction([](float value, int) { return juce::String(value * 100.0f, 1); })
                  .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue() / 100.0f; })),
          std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode", 1}, "Focus",
              juce::StringArray{"Low", "Full", "High"}, 1)})
{
    amountValue = parameters.getRawParameterValue("amount");
    modeValue = parameters.getRawParameterValue("mode");
}

void EmberProcessor::prepareToPlay(double rate, int)
{
    engine.prepare(rate);
    setLatencySamples(engine.isReady() && engine.rateGroup() < 2 ? 4 : 0);
}
bool EmberProcessor::isBusesLayoutSupported(const BusesLayout& b) const
{
    return b.getMainInputChannelSet() == b.getMainOutputChannelSet()
        && (b.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
            || b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
}
void EmberProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    processAudio(audio, midi, 0.0f);
}
void EmberProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    processAudio(audio, midi, 1.0f);
}
void EmberProcessor::processAudio(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, float bypass)
{
    juce::ScopedNoDenormals denormals;
    midi.clear();
    if (!engine.isReady())
    {
        audio.clear();
        return;
    }
    const bool mono = getTotalNumInputChannels() == 1;
    auto* left = audio.getWritePointer(0);
    auto* right = mono ? left : audio.getWritePointer(1);
    engine.process(left, right, left, right, audio.getNumSamples(),
        amountValue->load(), modeValue->load() * 0.5f, bypass, mono, mono);
}
juce::AudioProcessorEditor* EmberProcessor::createEditor() { return new EmberEditor(*this); }
void EmberProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = parameters.copyState().createXml())
    {
        xml->setAttribute("amountBits", juce::String(static_cast<juce::int64>(std::bit_cast<std::uint32_t>(amountValue->load()))));
        copyXmlToBinary(*xml, dest);
    }
}
void EmberProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName("EmberState"))
    {
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
        if (xml->hasAttribute("amountBits"))
        {
            const auto bits = static_cast<std::uint32_t>(xml->getStringAttribute("amountBits").getLargeIntValue());
            const float value = std::bit_cast<float>(bits);
            if (std::isfinite(value) && value >= 0.0f && value <= 1.0f)
                parameters.getParameter("amount")->setValueNotifyingHost(value);
        }
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EmberProcessor(); }
