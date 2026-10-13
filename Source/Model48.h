#pragma once

#include "Model44.h"

namespace ember
{
// The 48 kHz family shares the float pipeline and mutable state layout.
class Model48
{
public:
    using State = Model44::State;
    using Tables = Model44::Tables;
    using Controls = Model44::Controls;
    inline static constexpr Model44::Tuning tuning {
        0x1.3dc014p-17f, 0x1.b4e81cp-12f, 0x1.b4e81cp-5f,
        0x1.fe53c4p-1f, 0x1.ffc3f6p-1f, 0x1.ffd51ep-1f,
        0x1.eb01a4p+7f
    };
    static void update(State& state, const Tables& tables, Controls& controls,
                       int frames, bool immediate, bool blockSlew) noexcept
    {
        Model44::update(state, tables, controls, frames, immediate, blockSlew, tuning);
    }
    static void processStereo(State& state, const Tables& tables,
                              const float* inputLeft, const float* inputRight,
                              float* outputLeft, float* outputRight,
                              int frames, float outputGain) noexcept
    {
        Model44::processStereo(state, tables, inputLeft, inputRight, outputLeft,
                               outputRight, frames, outputGain, tuning);
    }
    static void processMono(State& state, const Tables& tables, const float* input,
                            float* output, int frames, float outputGain) noexcept
    {
        Model44::processMono(state, tables, input, output, frames, outputGain, tuning);
    }
    static void processChannels(State& state, const Tables& tables,
                                const float* inputLeft, const float* inputRight,
                                float* outputLeft, float* outputRight, int frames,
                                float outputGain, bool inputMono, bool outputMono) noexcept
    {
        Model44::processChannels(state, tables, inputLeft, inputRight, outputLeft,
                                outputRight, frames, outputGain, inputMono, outputMono, tuning);
    }
};
}
