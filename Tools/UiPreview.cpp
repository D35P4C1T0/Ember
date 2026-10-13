#include "Processor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc != 2) { std::cerr << "UiPreview <new-output.png>\n"; return 2; }
    EmberProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setVisible(true);
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    if (file.exists()) { std::cerr << "Output already exists\n"; return 2; }
    juce::FileOutputStream output(file);
    if (!output.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, output)) return 1;
    return 0;
}
