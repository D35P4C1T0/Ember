#pragma once

#include "ControlStages.h"
#include "FilterStages.h"
#include "LookupStage.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace ember
{
// Float processing models for the four higher sample-rate families. Callers
// preserve each family's host block boundaries and use group indices 2 through 5.
// Compile without fast math or floating-point contraction.
class ModelHighRate
{
public:
    struct Tuning
    {
        float activityDecay;
        float bypassStep;
        float controlSlew;
        float shapingRelease;
        float meterRelease;
        float gainSlew;
        float meterScale;
    };
    inline static constexpr std::array<Tuning, 4> tunings {{
        { 0x1.59d9c6p-18f, 0x1.db8b7p-13f, 0x1.db8b7p-5f,
          0x1.fe2df8p-1f, 0x1.ffbea8p-1f, 0x1.ffd152p-1f, 0x1.fe0bf8p+5f },
        { 0x1.3dc014p-18f, 0x1.b4e81cp-13f, 0x1.b4e81cp-5f,
          0x1.fe53c4p-1f, 0x1.ffc3f6p-1f, 0x1.ffd51ep-1f, 0x1.fe33a4p+5f },
        { 0x1.59d9c6p-19f, 0x1.db8b7p-14f, 0x1.db8b7p-5f,
          0x1.ff16c6p-1f, 0x1.ffdf52p-1f, 0x1.ffe8a8p-1f, 0x1.feffe2p+5f },
        { 0x1.3dc014p-19f, 0x1.b4e81cp-14f, 0x1.b4e81cp-5f,
          0x1.ff29b6p-1f, 0x1.ffe1fap-1f, 0x1.ffea8ep-1f, 0x1.ff13c4p+5f }
    }};
    static constexpr int maximumFrames(unsigned group) noexcept
    {
        return group == 2 || group == 3 ? 512 : group == 4 || group == 5 ? 1024 : 0;
    }
    struct Tables
    {
        const float* resampling; // 73 values; unused by this processing graph
        const float* controls;   // 234 values with interpolation guard entries
        const float* shape;      // 25 rows of 65 values
    };
    struct State
    {
        std::array<float, 56> audio{};
        std::array<float, 13> tracking{};
        std::array<float, 27> derived{};
        std::array<float, 2> meters{};
        std::array<float, 16> filters{};
    };
    struct Controls
    {
        float amount;
        float mode;
        float auxiliary;
        float bypass;
        float envelope;
    };

    static void update(State& state, const Tables& tables, Controls& controls,
                       int frames, bool immediate, bool blockSlew,
                       unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        auto& h = state.tracking;
        auto& d = state.derived;
        const float init = immediate ? 1.0f : 0.0f;
        d[0] = std::bit_cast<float>(static_cast<std::uint32_t>(immediate));
        d[1] = controls.bypass;
        float changed = 0.0f;
        const std::array<float, 4> target { controls.amount, controls.mode,
                                           controls.auxiliary, controls.bypass };
        for (int i = 0; i < 4; ++i)
        {
            const float threshold = i == 2 ? 0.01f : 0x1.fff5cap-24f;
            if (absolute(target[i] - h[i]) > threshold)
            {
                h[i] = target[i];
                if (i == 2 || !immediate)
                    changed = 1.0f;
            }
        }
        d[2] = changed;
        float meter = state.meters[0] * tuning.meterScale;
        if (meter <= 0x1.21a186p-3f) meter = 0x1.21a186p-3f;
        if (meter >= 0x1.fadaa8p-3f) meter = 0x1.fadaa8p-3f;
        meter = (meter - 0x1.21a186p-3f) * 0x1.2db2eap+3f;
        if (meter <= 0.0f) meter = 0.0f;
        if (meter >= 1.0f) meter = 1.0f;
        float slew = blockSlew ? static_cast<float>(frames) * tuning.controlSlew : 1.0f;
        if (immediate) slew = 1.0f;
        auto follow = [slew](float current, float targetValue) noexcept
        {
            if (targetValue > current)
                return std::min(current + slew, targetValue);
            return std::max(current - slew, targetValue);
        };
        auto curve = [](const float* table, float value, float multiplier) noexcept
        {
            float position = value * multiplier;
            int index = static_cast<int>(position);
            if (position < 0.0f) --index;
            const float fraction = position - static_cast<float>(index);
            float output = table[index + 1] - table[index];
            output = output * fraction;
            return output + table[index];
        };
        if (absolute(h[5] - controls.amount) + init > 1.0e-30f)
        {
            h[5] = follow(h[5], controls.amount);
            d[3] = curve(tables.controls, h[5], 23.0f);
        }
        if (absolute(h[6] - controls.amount) + init > 1.0e-30f)
        {
            h[6] = follow(h[6], controls.amount);
            d[4] = curve(tables.controls + 25, h[6], 23.0f);
        }
        if (absolute(h[7] - controls.amount) + init > 1.0e-30f)
        {
            h[7] = follow(h[7], controls.amount);
            d[5] = curve(tables.controls + 50, h[7], 10.0f);
        }
        for (int i = 0; i < 4; ++i)
        {
            const int trackingIndex = i < 2 ? 8 + i : 9 + i;
            const int outputIndex = i < 2 ? 6 + i * 5 : 17 + (i - 2) * 5;
            const int tableIndex = i < 2 ? 62 + i * 40 : 154 + (i - 2) * 40;
            if (absolute(h[trackingIndex] - controls.mode) + init > 1.0e-30f)
            {
                h[trackingIndex] = follow(h[trackingIndex], controls.mode);
                const float* table = tables.controls + tableIndex;
                ControlStages::interpolateRows(table, 3, table + 25, 3,
                                               d.data() + outputIndex, h[trackingIndex]);
                ControlStages::interpolateRows(table, 3, table + 34, 2,
                                               d.data() + outputIndex + 3, h[trackingIndex]);
            }
        }
        if (absolute(h[10] - controls.amount) + init > 1.0e-30f)
        {
            h[10] = follow(h[10], controls.amount);
            d[16] = curve(tables.controls + 142, h[10], 10.0f);
        }
        controls.auxiliary = meter;
        controls.envelope = state.meters[1] > changed ? state.meters[1] : changed;
    }

    static void processStereo(State& state, const Tables& tables,
                              const float* inputLeft, const float* inputRight,
                              float* outputLeft, float* outputRight,
                              int frames, float outputGain,
                              unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        if (frames <= 0 || frames > maximumFrames(group)) return;
        auto& a = state.audio;
        auto& d = state.derived;
        auto& f = state.filters;
        std::array<float, 1024> left, right, parallelLeft, parallelRight;
        std::array<float, 1024> dryLeft, dryRight, bypass, wet;
        std::array<float, 1024> scratch;
        std::array<int, 1024> indices;
        std::array<float, 10> coefficients;
        std::copy_n(inputLeft, frames, left.data());
        std::copy_n(inputRight, frames, right.data());
        const bool immediate = std::bit_cast<std::uint32_t>(d[0]) != 0;
        const float init = immediate ? 1.0f : 0.0f;
        d[0] = 0.0f;
        float active = a[2] - static_cast<float>(frames) * tuning.activityDecay;
        active = active > d[2] ? active : d[2];
        ControlStages::targetRamp(a[0], bypass.data(), frames, d[1],
                                  tuning.bypassStep, immediate);
        for (int i = 0; i < frames; ++i)
        {
            dryLeft[i] = inputLeft[i] * bypass[i];
            dryRight[i] = inputRight[i] * bypass[i];
            wet[i] = bypass[i] * -1.0f;
            wet[i] = wet[i] + 1.0f;
            left[i] = left[i] * wet[i];
            right[i] = right[i] * wet[i];
        }
        const bool entirelyDry = absolute(wet[0]) < 0x1.fff5cap-24f
                              && absolute(wet[frames - 1]) < 0x1.fff5cap-24f;
        if (!entirelyDry)
        {
            const float inputPeak = std::max(peak(left.data(), frames), peak(right.data(), frames));
            if (inputPeak > 5.0e-8f) active = 1.0f;
        }
        float gate = immediate ? 1.0f : active;
        gate = gate > 0.0f ? gate : 0.0f;
        if (gate >= 0.5f)
        {
            const int count = frames;
            const float slew = integerPower((1.0f - init) * tuning.gainSlew, count);
            const float reciprocal = 1.0f / static_cast<float>(count);
            std::copy_n(left.data(), count, parallelLeft.data());
            std::copy_n(right.data(), count, parallelRight.data());
            gain(parallelLeft.data(), parallelRight.data(), count, d[3], a[4], slew, reciprocal, init);
            gain(left.data(), right.data(), count, d[4], a[6], slew, reciprocal, init);
            gain(left.data(), right.data(), count, d[5], a[8], slew, reciprocal, init);
            ControlStages::coefficientRamp(5, 4, d.data() + 6, f.data() + 0,
                                           a.data() + 10, slew, reciprocal, coefficients.data(), init);
            FilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 0, true);
            ControlStages::coefficientRamp(5, 4, d.data() + 11, f.data() + 4,
                                           a.data() + 20, slew, reciprocal, coefficients.data(), init);
            FilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 4, true);
            float envelope = a[30];
            for (int i = 0; i < count; ++i)
            {
                const float value = std::max(absolute(left[i]), absolute(right[i]));
                envelope = (envelope - value) * tuning.meterRelease + value;
            }
            a[30] = envelope;
            state.meters[0] = envelope;
            LookupStage::process(left.data(), count, tuning.shapingRelease, a[32], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0f, 26);
            LookupStage::process(right.data(), count, tuning.shapingRelease, a[33], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0f, 26);
            gain(left.data(), right.data(), count, d[16], a[34], slew, reciprocal, init);
            ControlStages::coefficientRamp(5, 4, d.data() + 17, f.data() + 8,
                                           a.data() + 36, slew, reciprocal, coefficients.data(), init);
            FilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 8, true);
            ControlStages::coefficientRamp(5, 4, d.data() + 22, f.data() + 12,
                                           a.data() + 46, slew, reciprocal, coefficients.data(), init);
            FilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 12, true);
            for (int i = 0; i < count; ++i)
            {
                left[i] = parallelLeft[i] + left[i];
                right[i] = parallelRight[i] + right[i];
            }
            for (int i = 0; i < frames; ++i)
            {
                left[i] = left[i] * outputGain;
                right[i] = right[i] * outputGain;
            }
            if (std::max(peak(left.data(), frames), peak(right.data(), frames)) > 5.0e-8f)
                active = 1.0f;
            for (int i = 0; i < frames; ++i)
            {
                left[i] = left[i] * wet[i]; right[i] = right[i] * wet[i];
            }
        }
        else
        {
            std::fill_n(left.data(), frames, 0.0f);
            std::fill_n(right.data(), frames, 0.0f);
        }
        a[2] = active;
        state.meters[1] = gate;
        for (int i = 0; i < frames; ++i)
        {
            outputLeft[i] = dryLeft[i] + left[i];
            outputRight[i] = dryRight[i] + right[i];
        }
    }


    static void processMono(State& state, const Tables& tables,
                              const float* input,
                              float* output,
                              int frames, float outputGain,
                              unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        if (frames <= 0 || frames > maximumFrames(group)) return;
        auto& a = state.audio;
        auto& d = state.derived;
        auto& f = state.filters;
        std::array<float, 1024> samples, parallel;
        std::array<float, 1024> dry, bypass, wet;
        std::array<float, 1024> scratch;
        std::array<int, 1024> indices;
        std::array<float, 10> coefficients;
        std::copy_n(input, frames, samples.data());
        const bool immediate = std::bit_cast<std::uint32_t>(d[0]) != 0;
        const float init = immediate ? 1.0f : 0.0f;
        d[0] = 0.0f;
        float active = a[2] - static_cast<float>(frames) * tuning.activityDecay;
        active = active > d[2] ? active : d[2];
        ControlStages::targetRamp(a[0], bypass.data(), frames, d[1],
                                  tuning.bypassStep, immediate);
        for (int i = 0; i < frames; ++i)
        {
            dry[i] = input[i] * bypass[i];
            wet[i] = bypass[i] * -1.0f;
            wet[i] = wet[i] + 1.0f;
            samples[i] = samples[i] * wet[i];
        }
        const bool entirelyDry = absolute(wet[0]) < 0x1.fff5cap-24f
                              && absolute(wet[frames - 1]) < 0x1.fff5cap-24f;
        if (!entirelyDry)
        {
            const float inputPeak = peak(samples.data(), frames);
            if (inputPeak > 5.0e-8f) active = 1.0f;
        }
        float gate = immediate ? 1.0f : active;
        gate = gate > 0.0f ? gate : 0.0f;
        if (gate >= 0.5f)
        {
            const int count = frames;
            const float slew = integerPower((1.0f - init) * tuning.gainSlew, count);
            const float reciprocal = 1.0f / static_cast<float>(count);
            std::copy_n(samples.data(), count, parallel.data());
            gain(parallel.data(), nullptr, count, d[3], a[4], slew, reciprocal, init);
            gain(samples.data(), nullptr, count, d[4], a[6], slew, reciprocal, init);
            gain(samples.data(), nullptr, count, d[5], a[8], slew, reciprocal, init);
            ControlStages::coefficientRamp(5, 2, d.data() + 6, f.data() + 0,
                                           a.data() + 10, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 0, true);
            ControlStages::coefficientRamp(5, 2, d.data() + 11, f.data() + 4,
                                           a.data() + 20, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 4, true);
            float envelope = a[30];
            for (int i = 0; i < count; ++i)
            {
                const float value = absolute(samples[i]);
                envelope = (envelope - value) * tuning.meterRelease + value;
            }
            a[30] = envelope;
            state.meters[0] = envelope;
            LookupStage::process(samples.data(), count, tuning.shapingRelease, a[32], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0f, 26);
            gain(samples.data(), nullptr, count, d[16], a[34], slew, reciprocal, init);
            ControlStages::coefficientRamp(5, 2, d.data() + 17, f.data() + 8,
                                           a.data() + 36, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 8, true);
            ControlStages::coefficientRamp(5, 2, d.data() + 22, f.data() + 12,
                                           a.data() + 46, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 12, true);
            for (int i = 0; i < count; ++i)
            {
                samples[i] = parallel[i] + samples[i];
            }
            for (int i = 0; i < frames; ++i)
            {
                samples[i] = samples[i] * outputGain;
            }
            if (peak(samples.data(), frames) > 5.0e-8f)
                active = 1.0f;
            for (int i = 0; i < frames; ++i)
            {
                samples[i] = samples[i] * wet[i];
            }
        }
        else
        {
            std::fill_n(samples.data(), frames, 0.0f);
        }
        a[2] = active;
        state.meters[1] = gate;
        for (int i = 0; i < frames; ++i)
        {
            output[i] = dry[i] + samples[i];
        }
    }

    // Output folding is a sum, including when a mono input is duplicated.
    static void processChannels(State& state, const Tables& tables,
                                const float* inputLeft, const float* inputRight,
                                float* outputLeft, float* outputRight, int frames,
                                float outputGain, bool inputMono, bool outputMono,
                                unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        if (frames <= 0 || frames > maximumFrames(group)) return;
        if (inputMono)
        {
            processMono(state, tables, inputLeft, outputLeft, frames, outputGain, group);
            if (outputMono)
                for (int i = 0; i < frames; ++i) outputLeft[i] = outputLeft[i] + outputLeft[i];
            else
                std::copy_n(outputLeft, frames, outputRight);
        }
        else if (outputMono)
        {
            std::array<float, 1024> right;
            processStereo(state, tables, inputLeft, inputRight, outputLeft,
                          right.data(), frames, outputGain, group);
            for (int i = 0; i < frames; ++i) outputLeft[i] = right[i] + outputLeft[i];
        }
        else
            processStereo(state, tables, inputLeft, inputRight, outputLeft,
                          outputRight, frames, outputGain, group);
    }

private:
    static void accumulatingMono(float* samples, int count, const float* coefficients,
                                 float* state, bool ramp) noexcept
    {
        float c0 = coefficients[0], c1 = coefficients[1], c2 = coefficients[2];
        float c3 = coefficients[3], c4 = coefficients[4];
        float first = state[0], second = state[1];
        for (int i = 0; i < count; ++i)
        {
            const float input = samples[i];
            if (ramp)
            {
                c0 = c0 + coefficients[5]; c1 = c1 + coefficients[6];
                c2 = c2 + coefficients[7]; c3 = c3 + coefficients[8];
                c4 = c4 + coefficients[9];
            }
            float output = input * c0;
            output = output + first;
            float sum = second + first;
            float nextFirst = input * c1;
            nextFirst = nextFirst + sum;
            float nextSecond = input * c2;
            nextSecond = nextSecond + second;
            samples[i] = output;
            first = nextFirst + output * c3;
            second = nextSecond + output * c4;
        }
        state[0] = absolute(first) < 1.0e-30f ? 0.0f : first;
        state[1] = absolute(second) < 1.0e-30f ? 0.0f : second;
    }
    static float absolute(float value) noexcept
    {
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
    }
    static float peak(const float* values, int count) noexcept
    {
        float value = absolute(values[0]);
        if (value <= 5.0e-8f)
        {
            value = 0.0f;
            for (int i = 0; i < count; ++i)
                value = std::max(value, absolute(values[i]));
        }
        return value;
    }
    static float integerPower(float base, int exponent) noexcept
    {
        float output = 1.0f;
        while (exponent != 0)
        {
            if (exponent & 1)
            {
                output = output * base;
                if (exponent == 1) break;
            }
            exponent >>= 1;
            base = base * base;
        }
        return output;
    }
    static void gain(float* left, float* right, int count, float target,
                     float& current, float slew, float reciprocal, float mix) noexcept
    {
        float first = (1.0f - mix) * current;
        first = first + target * mix;
        float end = (first - target) * slew;
        end = end + target;
        current = end;
        float step = (end - first) * reciprocal;
        if (absolute(step) > 1.0e-30f)
        {
            std::array<float, 4> lanes;
            for (int lane = 0; lane < 4; ++lane)
                lanes[lane] = static_cast<float>(lane + 1) * step + first;
            const float groupStep = 4.0f * step;
            int i = 0;
            for (; i < (count & ~3); i += 4)
            {
                for (int lane = 0; lane < 4; ++lane)
                {
                    const float multiplier = lanes[lane];
                    left[i + lane] = left[i + lane] * multiplier;
                    if (right != nullptr) right[i + lane] = right[i + lane] * multiplier;
                    lanes[lane] = multiplier + groupStep;
                }
            }
            for (; i < count; ++i)
            {
                const float multiplier = static_cast<float>(i + 1) * step + first;
                left[i] = multiplier * left[i];
                if (right != nullptr) right[i] = multiplier * right[i];
            }
        }
        else if (absolute(first - 1.0f) > 1.0e-30f)
        {
            if (absolute(first) < 1.0e-30f)
            {
                std::fill_n(left, count, 0.0f);
                if (right != nullptr) std::fill_n(right, count, 0.0f);
            }
            else
                for (int i = 0; i < count; ++i)
                {
                    left[i] = first * left[i];
                    if (right != nullptr) right[i] = first * right[i];
                }
        }
    }
};
}
