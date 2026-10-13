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
// Float processing model for the 44.1 kHz coefficient family. Buffers are bounded
// to 256 frames, and callers preserve host block boundaries when calling it.
// Compile without fast math or floating-point contraction.
class Model44
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
    inline static constexpr Tuning defaultTuning {
        0x1.59d9c6p-17f, 0x1.db8b7p-12f, 0x1.db8b7p-5f,
        0x1.fe2df8p-1f, 0x1.ffbea8p-1f, 0x1.ffd152p-1f,
        0x1.e7966cp+7f
    };
    struct Tables
    {
        const float* resampling; // 172 values
        const float* controls;   // 234 values with interpolation guard entries
        const float* shape;      // 25 rows of 65 values
    };
    struct State
    {
        std::array<float, 66> audio{};
        std::array<float, 13> tracking{};
        std::array<float, 27> derived{};
        std::array<float, 2> meters{};
        std::array<float, 56> filters{};
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
                       const Tuning& tuning = defaultTuning) noexcept
    {
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
                              const Tuning& tuning = defaultTuning) noexcept
    {
        if (frames <= 0 || frames > 256) return;
        auto& a = state.audio;
        auto& d = state.derived;
        auto& f = state.filters;
        std::array<float, 512> left, right, parallelLeft, parallelRight;
        std::array<float, 256> dryLeft, dryRight, bypass, wet;
        std::array<float, 512> scratch;
        std::array<int, 512> indices;
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
        delay(a, 4, 6, inputLeft, dryLeft.data(), frames);
        delay(a, 5, 10, inputRight, dryRight.data(), frames);
        for (int i = 0; i < frames; ++i)
        {
            dryLeft[i] = dryLeft[i] * bypass[i];
            dryRight[i] = dryRight[i] * bypass[i];
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
            for (int i = frames - 1; i >= 0; --i)
            {
                left[i * 2] = left[i]; right[i * 2] = right[i];
                left[i * 2 + 1] = 0.0f; right[i * 2 + 1] = 0.0f;
            }
            const int count = frames * 2;
            FilterStages::cascade(left.data(), right.data(), count, tables.resampling, f.data(), 5);
            const float slew = integerPower((1.0f - init) * tuning.gainSlew, count);
            const float reciprocal = 1.0f / static_cast<float>(count);
            std::copy_n(left.data(), count, parallelLeft.data());
            std::copy_n(right.data(), count, parallelRight.data());
            gain(parallelLeft.data(), parallelRight.data(), count, d[3], a[14], slew, reciprocal, init);
            gain(left.data(), right.data(), count, d[4], a[16], slew, reciprocal, init);
            gain(left.data(), right.data(), count, d[5], a[18], slew, reciprocal, init);
            ControlStages::coefficientRamp(5, 4, d.data() + 6, f.data() + 20,
                                           a.data() + 20, slew, reciprocal, coefficients.data(), init);
            FilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 20, true);
            ControlStages::coefficientRamp(5, 4, d.data() + 11, f.data() + 24,
                                           a.data() + 30, slew, reciprocal, coefficients.data(), init);
            FilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 24, true);
            float envelope = a[40];
            for (int i = 0; i < count; ++i)
            {
                const float value = std::max(absolute(left[i]), absolute(right[i]));
                envelope = (envelope - value) * tuning.meterRelease + value;
            }
            a[40] = envelope;
            state.meters[0] = envelope;
            LookupStage::process(left.data(), count, tuning.shapingRelease, a[42], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0f, 101);
            LookupStage::process(right.data(), count, tuning.shapingRelease, a[43], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0f, 101);
            gain(left.data(), right.data(), count, d[16], a[44], slew, reciprocal, init);
            ControlStages::coefficientRamp(5, 4, d.data() + 17, f.data() + 28,
                                           a.data() + 46, slew, reciprocal, coefficients.data(), init);
            FilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 28, true);
            ControlStages::coefficientRamp(5, 4, d.data() + 22, f.data() + 32,
                                           a.data() + 56, slew, reciprocal, coefficients.data(), init);
            FilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 32, true);
            for (int i = 0; i < count; ++i)
            {
                left[i] = parallelLeft[i] + left[i];
                right[i] = parallelRight[i] + right[i];
            }
            FilterStages::cascade(left.data(), right.data(), count, tables.resampling + 25, f.data() + 36, 5);
            for (int i = 0; i < frames; ++i)
            {
                left[i] = left[i * 2] * outputGain;
                right[i] = right[i * 2] * outputGain;
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
                              const Tuning& tuning = defaultTuning) noexcept
    {
        if (frames <= 0 || frames > 256) return;
        auto& a = state.audio;
        auto& d = state.derived;
        auto& f = state.filters;
        std::array<float, 512> samples, parallel;
        std::array<float, 256> dry, bypass, wet;
        std::array<float, 512> scratch;
        std::array<int, 512> indices;
        std::array<float, 10> coefficients;
        std::copy_n(input, frames, samples.data());
        const bool immediate = std::bit_cast<std::uint32_t>(d[0]) != 0;
        const float init = immediate ? 1.0f : 0.0f;
        d[0] = 0.0f;
        float active = a[2] - static_cast<float>(frames) * tuning.activityDecay;
        active = active > d[2] ? active : d[2];
        ControlStages::targetRamp(a[0], bypass.data(), frames, d[1],
                                  tuning.bypassStep, immediate);
        delay(a, 4, 6, input, dry.data(), frames);
        for (int i = 0; i < frames; ++i)
        {
            dry[i] = dry[i] * bypass[i];
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
            for (int i = frames - 1; i >= 0; --i)
            {
                samples[i * 2] = samples[i];
                samples[i * 2 + 1] = 0.0f;
            }
            const int count = frames * 2;
            cascadeMono(samples.data(), count, tables.resampling, f.data(), 5);
            const float slew = integerPower((1.0f - init) * tuning.gainSlew, count);
            const float reciprocal = 1.0f / static_cast<float>(count);
            std::copy_n(samples.data(), count, parallel.data());
            gain(parallel.data(), nullptr, count, d[3], a[14], slew, reciprocal, init);
            gain(samples.data(), nullptr, count, d[4], a[16], slew, reciprocal, init);
            gain(samples.data(), nullptr, count, d[5], a[18], slew, reciprocal, init);
            ControlStages::coefficientRamp(5, 2, d.data() + 6, f.data() + 20,
                                           a.data() + 20, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 20, true);
            ControlStages::coefficientRamp(5, 2, d.data() + 11, f.data() + 24,
                                           a.data() + 30, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 24, true);
            float envelope = a[40];
            for (int i = 0; i < count; ++i)
            {
                const float value = absolute(samples[i]);
                envelope = (envelope - value) * tuning.meterRelease + value;
            }
            a[40] = envelope;
            state.meters[0] = envelope;
            LookupStage::process(samples.data(), count, tuning.shapingRelease, a[42], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0f, 101);
            gain(samples.data(), nullptr, count, d[16], a[44], slew, reciprocal, init);
            ControlStages::coefficientRamp(5, 2, d.data() + 17, f.data() + 28,
                                           a.data() + 46, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 28, true);
            ControlStages::coefficientRamp(5, 2, d.data() + 22, f.data() + 32,
                                           a.data() + 56, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 32, true);
            for (int i = 0; i < count; ++i)
            {
                samples[i] = parallel[i] + samples[i];
            }
            cascadeMono(samples.data(), count, tables.resampling + 25, f.data() + 36, 5);
            for (int i = 0; i < frames; ++i)
            {
                samples[i] = samples[i * 2] * outputGain;
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
                                const Tuning& tuning = defaultTuning) noexcept
    {
        if (frames <= 0 || frames > 256) return;
        if (inputMono)
        {
            processMono(state, tables, inputLeft, outputLeft, frames, outputGain, tuning);
            if (outputMono)
                for (int i = 0; i < frames; ++i) outputLeft[i] = outputLeft[i] + outputLeft[i];
            else
                std::copy_n(outputLeft, frames, outputRight);
        }
        else if (outputMono)
        {
            std::array<float, 256> right;
            processStereo(state, tables, inputLeft, inputRight, outputLeft,
                          right.data(), frames, outputGain, tuning);
            for (int i = 0; i < frames; ++i) outputLeft[i] = right[i] + outputLeft[i];
        }
        else
            processStereo(state, tables, inputLeft, inputRight, outputLeft,
                          outputRight, frames, outputGain, tuning);
    }

private:
    static void cascadeMono(float* samples, int count, const float* coefficients,
                            float* state, int sections) noexcept
    {
        for (int section = 0; section < sections; ++section)
        {
            const float* c = coefficients + section * 5;
            float* s = state + section * 2;
            float previous = s[0], older = s[1];
            for (int i = 0; i < count; ++i)
            {
                float current = c[1] * older;
                current = current + samples[i];
                current = current + c[0] * previous;
                float output = c[2] * current;
                output = output + c[3] * previous;
                output = output + c[4] * older;
                samples[i] = output;
                older = previous;
                previous = current;
            }
            s[0] = absolute(previous) < 1.0e-30f ? 0.0f : previous;
            s[1] = absolute(older) < 1.0e-30f ? 0.0f : older;
        }
    }
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
    static void delay(std::array<float, 66>& state, int indexSlot, int samplesSlot,
                      const float* input, float* output, int count) noexcept
    {
        int index = std::bit_cast<int>(state[indexSlot]);
        for (int i = 0; i < count; ++i)
        {
            output[i] = state[samplesSlot + index];
            state[samplesSlot + index] = input[i];
            if (++index >= 4) index = 0;
        }
        state[indexSlot] = std::bit_cast<float>(index);
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
