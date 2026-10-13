#pragma once

#include "BlockScheduler.h"
#include "Model48.h"
#include "ModelHighRate.h"
#include "ModelTables.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ember
{
// Owns preallocated model state and the persistent control clock.
class FloatEngine
{
public:
    bool prepare(double rate) noexcept
    {
        constexpr std::array<double, 6> rates {44100, 48000, 88200, 96000, 176400, 192000};
        ready = std::isfinite(rate) && rate > 0.0;
        if (!ready) return false;
        const auto found = std::find(rates.begin(), rates.end(), std::trunc(rate));
        group = found == rates.end() ? 0u : static_cast<unsigned>(found - rates.begin());
        const auto& data = *floatModelTables(group);
        tables = {data[0].data(), data[2].data(), data[6].data()};
        highTables = {data[0].data(), data[2].data(), data[6].data()};
        const unsigned derived = data[4].size() == 27 ? 4 : 5;
        auto initialize = [&](auto& state)
        {
            std::copy(data[1].begin(), data[1].end(), state.audio.begin());
            std::copy(data[3].begin(), data[3].end(), state.tracking.begin());
            std::copy(data[derived].begin(), data[derived].end(), state.derived.begin());
            std::copy(data[derived == 4 ? 5 : 4].begin(), data[derived == 4 ? 5 : 4].end(), state.meters.begin());
            std::copy(data[7].begin(), data[7].end(), state.filters.begin());
        };
        if (group < 2) initialize(lowState); else initialize(highState);
        controls = {0.0f, 0.5f, 0.0f, 0.0f, 1.0f};
        highControls = {0.0f, 0.5f, 0.0f, 0.0f, 1.0f};
        clock = BlockScheduler(group < 2 ? 256 : ModelHighRate::maximumFrames(group));
        initialized = false;
        return true;
    }

    bool isReady() const noexcept { return ready; }
    unsigned rateGroup() const noexcept { return group; }

    void process(const float* left, const float* right, float* outputLeft,
                 float* outputRight, int frames, float amount, float mode,
                 float bypass, bool inputMono, bool outputMono) noexcept
    {
        if (!ready) return;
        controls.amount = amount;
        controls.mode = mode;
        controls.bypass = bypass;
        highControls.amount = amount;
        highControls.mode = mode;
        highControls.bypass = bypass;
        if (!initialized)
        {
            clock.requestUpdate();
            initialized = true;
        }
        clock.process(frames, [&](int offset, int count)
        {
            const auto* r = inputMono ? left + offset : right + offset;
            auto* outR = outputMono ? outputLeft + offset : outputRight + offset;
            if (group < 2)
                Model44::processChannels(lowState, tables, left + offset, r,
                    outputLeft + offset, outR, count, 1.0f, inputMono, outputMono,
                    group == 0 ? Model44::defaultTuning : Model48::tuning);
            else
                ModelHighRate::processChannels(highState, highTables, left + offset, r,
                    outputLeft + offset, outR, count, 1.0f, inputMono, outputMono, group);
        }, [&](bool immediate)
        {
            if (group < 2)
                Model44::update(lowState, tables, controls, 1, immediate, true,
                    group == 0 ? Model44::defaultTuning : Model48::tuning);
            else
                ModelHighRate::update(highState, highTables, highControls, 1, immediate, true, group);
        });
        if (outputMono)
            for (int i = 0; i < frames; ++i) outputLeft[i] *= 0.5f;
    }

private:
    Model44::State lowState;
    Model44::Tables tables {};
    Model44::Controls controls {};
    ModelHighRate::State highState;
    ModelHighRate::Tables highTables {};
    ModelHighRate::Controls highControls {};
    BlockScheduler clock;
    unsigned group = 0;
    bool ready = false;
    bool initialized = false;
};
}
