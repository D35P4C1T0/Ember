#pragma once

#include "DoubleStages.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace ember
{
// Double internal processing model for the four high-rate coefficient families. Buffers are bounded
// to 1024 frames, and callers preserve host block boundaries when calling it.
// Compile without fast math or floating-point contraction.
class ModelDoubleHighRate
{
public:
    struct Tuning
    {
        double activityDecay;
        double bypassStep;
        double controlSlew;
        double shapingRelease;
        double meterRelease;
        double gainSlew;
        double meterScale;
    };
    inline static constexpr std::array<Tuning, 4> tunings {{
        { 0x1.59d9c581bf2c8p-18, 0x1.db8b6f9266dd4p-13, 0x1.db8b6f9266de8p-5, 0x1.fe2df750ebb14p-1, 0x1.ffbea7bbe59a5p-1, 0x1.ffd1526322a63p-1, 0x1.fe0bf7ce2d74bp+5 },
        { 0x1.3dc013dc013d6p-18, 0x1.b4e81b4e81b4ep-13, 0x1.b4e81b4e81b3ep-5, 0x1.fe53c4f358333p-1, 0x1.ffc3f6981dd1cp-1, 0x1.ffd51d22689ccp-1, 0x1.fe33a45966cbcp+5 },
        { 0x1.59d9c581bf2bcp-19, 0x1.db8b6f9266dd4p-14, 0x1.db8b6f9266de8p-5, 0x1.ff16c67df08bfp-1, 0x1.ffdf52d2f9fc3p-1, 0x1.ffe8a8a95cc1cp-1, 0x1.feffe106c9405p+5 },
        { 0x1.3dc013dc013d6p-19, 0x1.b4e81b4e81b4ep-14, 0x1.b4e81b4e81b3ep-5, 0x1.ff29b5982cbbep-1, 0x1.ffe1fa6ab560cp-1, 0x1.ffea8e1e37cfap-1, 0x1.ff13c35531c47p+5 }
    }};
    static constexpr int maximumFrames(unsigned group) noexcept
    {
        return group == 2 || group == 3 ? 512 : group == 4 || group == 5 ? 1024 : 0;
    }
    struct Tables
    {
        const double* resampling; // 73 values; unused by this processing graph
        const double* controls;   // 234 values with interpolation guard entries
        const double* shape;      // 25 rows of 65 values
    };
    struct State
    {
        std::array<double, 56> audio{};
        std::array<double, 13> tracking{};
        std::array<double, 27> derived{};
        std::array<double, 2> meters{};
        std::array<double, 16> filters{};
    };
    struct Controls
    {
        double amount;
        double mode;
        double auxiliary;
        double bypass;
        double envelope;
    };

    static void update(State& state, const Tables& tables, Controls& controls,
                       int frames, bool immediate, bool blockSlew,
                       unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        auto& h = state.tracking;
        auto& d = state.derived;
        const double init = immediate ? 1.0 : 0.0;
        d[0] = std::bit_cast<double>(static_cast<std::uint64_t>(immediate));
        d[1] = controls.bypass;
        double changed = 0.0;
        const std::array<double, 4> target { controls.amount, controls.mode,
                                           controls.auxiliary, controls.bypass };
        for (int i = 0; i < 4; ++i)
        {
            const double threshold = i == 2 ? 0x1.47ae147ae147bp-7 : 0x1.fff5cap-24;
            if (absolute(target[i] - h[i]) > threshold)
            {
                h[i] = target[i];
                if (i == 2 || !immediate)
                    changed = 1.0;
            }
        }
        d[2] = changed;
        double meter = state.meters[0] * tuning.meterScale;
        if (meter <= 0x1.21a1851ff630ap-3) meter = 0x1.21a1851ff630ap-3;
        if (meter >= 0x1.fadaa8f7eed50p-3) meter = 0x1.fadaa8f7eed50p-3;
        meter = (meter - 0x1.21a1851ff630ap-3) * 0x1.2db2eaabf5c82p+3;
        if (meter <= 0.0) meter = 0.0;
        if (meter >= 1.0) meter = 1.0;
        double slew = blockSlew ? static_cast<double>(frames) * tuning.controlSlew : 1.0;
        if (immediate) slew = 1.0;
        auto follow = [slew](double current, double targetValue) noexcept
        {
            if (targetValue > current)
                return std::min(current + slew, targetValue);
            return std::max(current - slew, targetValue);
        };
        auto curve = [](const double* table, double value, double multiplier) noexcept
        {
            double position = value * multiplier;
            int index = static_cast<int>(position);
            if (position < 0.0) --index;
            const double fraction = position - static_cast<double>(index);
            double output = table[index + 1] - table[index];
            output = output * fraction;
            return output + table[index];
        };
        if (absolute(h[5] - controls.amount) + init > 0x1.4484c00000000p-100)
        {
            h[5] = follow(h[5], controls.amount);
            d[3] = curve(tables.controls, h[5], 23.0);
        }
        if (absolute(h[6] - controls.amount) + init > 0x1.4484c00000000p-100)
        {
            h[6] = follow(h[6], controls.amount);
            d[4] = curve(tables.controls + 25, h[6], 23.0);
        }
        if (absolute(h[7] - controls.amount) + init > 0x1.4484c00000000p-100)
        {
            h[7] = follow(h[7], controls.amount);
            d[5] = curve(tables.controls + 50, h[7], 10.0);
        }
        for (int i = 0; i < 4; ++i)
        {
            const int trackingIndex = i < 2 ? 8 + i : 9 + i;
            const int outputIndex = i < 2 ? 6 + i * 5 : 17 + (i - 2) * 5;
            const int tableIndex = i < 2 ? 62 + i * 40 : 154 + (i - 2) * 40;
            if (absolute(h[trackingIndex] - controls.mode) + init > 0x1.4484c00000000p-100)
            {
                h[trackingIndex] = follow(h[trackingIndex], controls.mode);
                const double* table = tables.controls + tableIndex;
                DoubleControlStages::interpolateRows(table, 3, table + 25, 3,
                                               d.data() + outputIndex, h[trackingIndex]);
                DoubleControlStages::interpolateRows(table, 3, table + 34, 2,
                                               d.data() + outputIndex + 3, h[trackingIndex]);
            }
        }
        if (absolute(h[10] - controls.amount) + init > 0x1.4484c00000000p-100)
        {
            h[10] = follow(h[10], controls.amount);
            d[16] = curve(tables.controls + 142, h[10], 10.0);
        }
        controls.auxiliary = meter;
        controls.envelope = state.meters[1] > changed ? state.meters[1] : changed;
    }

    static void processStereoDouble(State& state, const Tables& tables,
                              const double* inputLeft, const double* inputRight,
                              double* outputLeft, double* outputRight,
                              int frames, double outputGain,
                              unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        if (frames <= 0 || frames > maximumFrames(group)) return;
        auto& a = state.audio;
        auto& d = state.derived;
        auto& f = state.filters;
        std::array<double, 1024> left, right, parallelLeft, parallelRight;
        std::array<double, 1024> dryLeft, dryRight, bypass, wet;
        std::array<double, 1024> scratch;
        std::array<int, 1024> indices;
        std::array<double, 10> coefficients;
        std::copy_n(inputLeft, frames, left.data());
        std::copy_n(inputRight, frames, right.data());
        const bool immediate = std::bit_cast<std::uint64_t>(d[0]) != 0;
        const double init = immediate ? 1.0 : 0.0;
        d[0] = 0.0;
        double active = a[2] - static_cast<double>(frames) * tuning.activityDecay;
        active = active > d[2] ? active : d[2];
        DoubleControlStages::targetRamp(a[0], bypass.data(), frames, d[1],
                                  tuning.bypassStep, immediate);
        for (int i = 0; i < frames; ++i)
        {
            dryLeft[i] = inputLeft[i] * bypass[i];
            dryRight[i] = inputRight[i] * bypass[i];
            wet[i] = bypass[i] * -1.0;
            wet[i] = wet[i] + 1.0;
            left[i] = left[i] * wet[i];
            right[i] = right[i] * wet[i];
        }
        const bool entirelyDry = absolute(wet[0]) < 0x1.fff5cap-24
                              && absolute(wet[frames - 1]) < 0x1.fff5cap-24;
        if (!entirelyDry)
        {
            const double inputPeak = std::max(peak(left.data(), frames), peak(right.data(), frames));
            if (inputPeak > 5.0e-8) active = 1.0;
        }
        double gate = immediate ? 1.0 : active;
        gate = gate > 0.0 ? gate : 0.0;
        if (gate >= 0.5)
        {
            const int count = frames;
            const double slew = integerPower((1.0 - init) * tuning.gainSlew, count);
            const double reciprocal = static_cast<double>(1.0f / static_cast<float>(count));
            std::copy_n(left.data(), count, parallelLeft.data());
            std::copy_n(right.data(), count, parallelRight.data());
            gain(parallelLeft.data(), parallelRight.data(), count, d[3], a[4], slew, reciprocal, init);
            gain(left.data(), right.data(), count, d[4], a[6], slew, reciprocal, init);
            gain(left.data(), right.data(), count, d[5], a[8], slew, reciprocal, init);
            DoubleControlStages::coefficientRamp(5, 4, d.data() + 6, f.data() + 0,
                                           a.data() + 10, slew, reciprocal, coefficients.data(), init);
            DoubleFilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 0, true);
            DoubleControlStages::coefficientRamp(5, 4, d.data() + 11, f.data() + 4,
                                           a.data() + 20, slew, reciprocal, coefficients.data(), init);
            DoubleFilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 4, true);
            double envelope = a[30];
            for (int i = 0; i < count; ++i)
            {
                const double value = std::max(absolute(left[i]), absolute(right[i]));
                envelope = (envelope - value) * tuning.meterRelease + value;
            }
            a[30] = envelope;
            state.meters[0] = envelope;
            DoubleLookupStage::process(left.data(), count, tuning.shapingRelease, a[32], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0, 26);
            DoubleLookupStage::process(right.data(), count, tuning.shapingRelease, a[33], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0, 26);
            gain(left.data(), right.data(), count, d[16], a[34], slew, reciprocal, init);
            DoubleControlStages::coefficientRamp(5, 4, d.data() + 17, f.data() + 8,
                                           a.data() + 36, slew, reciprocal, coefficients.data(), init);
            DoubleFilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 8, true);
            DoubleControlStages::coefficientRamp(5, 4, d.data() + 22, f.data() + 12,
                                           a.data() + 46, slew, reciprocal, coefficients.data(), init);
            DoubleFilterStages::accumulating(left.data(), right.data(), count, coefficients.data(), f.data() + 12, true);
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
            if (std::max(peak(left.data(), frames), peak(right.data(), frames)) > 5.0e-8)
                active = 1.0;
            for (int i = 0; i < frames; ++i)
            {
                left[i] = left[i] * wet[i]; right[i] = right[i] * wet[i];
            }
        }
        else
        {
            std::fill_n(left.data(), frames, 0.0);
            std::fill_n(right.data(), frames, 0.0);
        }
        a[2] = active;
        state.meters[1] = gate;
        for (int i = 0; i < frames; ++i)
        {
            outputLeft[i] = dryLeft[i] + left[i];
            outputRight[i] = dryRight[i] + right[i];
        }
    }

    static void processMonoDouble(State& state, const Tables& tables,
                              const double* input,
                              double* output,
                              int frames, double outputGain,
                              unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        if (frames <= 0 || frames > maximumFrames(group)) return;
        auto& a = state.audio;
        auto& d = state.derived;
        auto& f = state.filters;
        std::array<double, 1024> samples, parallel;
        std::array<double, 1024> dry, bypass, wet;
        std::array<double, 1024> scratch;
        std::array<int, 1024> indices;
        std::array<double, 10> coefficients;
        std::copy_n(input, frames, samples.data());
        const bool immediate = std::bit_cast<std::uint64_t>(d[0]) != 0;
        const double init = immediate ? 1.0 : 0.0;
        d[0] = 0.0;
        double active = a[2] - static_cast<double>(frames) * tuning.activityDecay;
        active = active > d[2] ? active : d[2];
        DoubleControlStages::targetRamp(a[0], bypass.data(), frames, d[1],
                                  tuning.bypassStep, immediate);
        for (int i = 0; i < frames; ++i)
        {
            dry[i] = input[i] * bypass[i];
            wet[i] = bypass[i] * -1.0;
            wet[i] = wet[i] + 1.0;
            samples[i] = samples[i] * wet[i];
        }
        const bool entirelyDry = absolute(wet[0]) < 0x1.fff5cap-24
                              && absolute(wet[frames - 1]) < 0x1.fff5cap-24;
        if (!entirelyDry)
        {
            const double inputPeak = peak(samples.data(), frames);
            if (inputPeak > 5.0e-8) active = 1.0;
        }
        double gate = immediate ? 1.0 : active;
        gate = gate > 0.0 ? gate : 0.0;
        if (gate >= 0.5)
        {
            const int count = frames;
            const double slew = integerPower((1.0 - init) * tuning.gainSlew, count);
            const double reciprocal = static_cast<double>(1.0f / static_cast<float>(count));
            std::copy_n(samples.data(), count, parallel.data());
            gain(parallel.data(), nullptr, count, d[3], a[4], slew, reciprocal, init);
            gain(samples.data(), nullptr, count, d[4], a[6], slew, reciprocal, init);
            gain(samples.data(), nullptr, count, d[5], a[8], slew, reciprocal, init);
            DoubleControlStages::coefficientRamp(5, 2, d.data() + 6, f.data() + 0,
                                           a.data() + 10, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 0, true);
            DoubleControlStages::coefficientRamp(5, 2, d.data() + 11, f.data() + 4,
                                           a.data() + 20, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 4, true);
            double envelope = a[30];
            for (int i = 0; i < count; ++i)
            {
                const double value = absolute(samples[i]);
                envelope = (envelope - value) * tuning.meterRelease + value;
            }
            a[30] = envelope;
            state.meters[0] = envelope;
            DoubleLookupStage::process(samples.data(), count, tuning.shapingRelease, a[32], tables.shape + 32,
                                  scratch.data(), indices.data(), 1.0, 26);
            gain(samples.data(), nullptr, count, d[16], a[34], slew, reciprocal, init);
            DoubleControlStages::coefficientRamp(5, 2, d.data() + 17, f.data() + 8,
                                           a.data() + 36, slew, reciprocal, coefficients.data(), init);
            accumulatingMono(samples.data(), count, coefficients.data(), f.data() + 8, true);
            DoubleControlStages::coefficientRamp(5, 2, d.data() + 22, f.data() + 12,
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
            if (peak(samples.data(), frames) > 5.0e-8)
                active = 1.0;
            for (int i = 0; i < frames; ++i)
            {
                samples[i] = samples[i] * wet[i];
            }
        }
        else
        {
            std::fill_n(samples.data(), frames, 0.0);
        }
        a[2] = active;
        state.meters[1] = gate;
        for (int i = 0; i < frames; ++i)
        {
            output[i] = dry[i] + samples[i];
        }
    }

    static void processStereo(State& state, const Tables& tables,
                              const float* left, const float* right,
                              float* outLeft, float* outRight, int frames, float gain,
                              unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        if (frames <= 0 || frames > maximumFrames(group)) return;
        std::array<double, 1024> l, r, ol, ore;
        for (int i = 0; i < frames; ++i) { l[i] = left[i]; r[i] = right[i]; }
        processStereoDouble(state, tables, l.data(), r.data(), ol.data(), ore.data(), frames, gain, group);
        for (int i = 0; i < frames; ++i) { outLeft[i] = static_cast<float>(ol[i]); outRight[i] = static_cast<float>(ore[i]); }
    }

    static void processMono(State& state, const Tables& tables,
                            const float* input, float* output, int frames, float gain,
                            unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        if (frames <= 0 || frames > maximumFrames(group)) return;
        std::array<double, 1024> samples, result;
        for (int i = 0; i < frames; ++i) samples[i] = input[i];
        processMonoDouble(state, tables, samples.data(), result.data(), frames, gain, group);
        for (int i = 0; i < frames; ++i) output[i] = static_cast<float>(result[i]);
    }

    // Output folding is a sum, including when a mono input is duplicated.
    static void processChannels(State& state, const Tables& tables,
                                const float* inputLeft, const float* inputRight,
                                float* outputLeft, float* outputRight, int frames,
                                float outputGain, bool inputMono, bool outputMono,
                                unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
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

    // Double sample boundaries retain channel folding in double.
    static void processChannelsDouble(State& state, const Tables& tables,
                                const double* inputLeft, const double* inputRight,
                                double* outputLeft, double* outputRight, int frames,
                                float outputGain, bool inputMono, bool outputMono,
                                unsigned group) noexcept
    {
        if (group < 2 || group > 5) return;
        const auto& tuning = tunings[group - 2];
        if (frames <= 0 || frames > maximumFrames(group)) return;
        if (inputMono)
        {
            processMonoDouble(state, tables, inputLeft, outputLeft, frames, outputGain, group);
            if (outputMono)
                for (int i = 0; i < frames; ++i) outputLeft[i] = outputLeft[i] + outputLeft[i];
            else
                std::copy_n(outputLeft, frames, outputRight);
        }
        else if (outputMono)
        {
            std::array<double, 1024> right;
            processStereoDouble(state, tables, inputLeft, inputRight, outputLeft,
                          right.data(), frames, outputGain, group);
            for (int i = 0; i < frames; ++i) outputLeft[i] = right[i] + outputLeft[i];
        }
        else
            processStereoDouble(state, tables, inputLeft, inputRight, outputLeft,
                          outputRight, frames, outputGain, group);
    }

private:
    static void accumulatingMono(double* samples, int count, const double* coefficients,
                                 double* state, bool ramp) noexcept
    {
        double c0 = coefficients[0], c1 = coefficients[1], c2 = coefficients[2];
        double c3 = coefficients[3], c4 = coefficients[4];
        double first = state[0], second = state[1];
        for (int i = 0; i < count; ++i)
        {
            const double input = samples[i];
            if (ramp)
            {
                c0 = c0 + coefficients[5]; c1 = c1 + coefficients[6];
                c2 = c2 + coefficients[7]; c3 = c3 + coefficients[8];
                c4 = c4 + coefficients[9];
            }
            double output = input * c0;
            output = output + first;
            double sum = second + first;
            double nextFirst = input * c1;
            nextFirst = nextFirst + sum;
            double nextSecond = input * c2;
            nextSecond = nextSecond + second;
            samples[i] = output;
            first = nextFirst + output * c3;
            second = nextSecond + output * c4;
        }
        state[0] = absolute(first) < 1.0e-300 ? 0.0 : first;
        state[1] = absolute(second) < 1.0e-300 ? 0.0 : second;
    }
    static double absolute(double value) noexcept
    {
        return std::bit_cast<double>(std::bit_cast<std::uint64_t>(value) & 0x7fffffffffffffffull);
    }
    static double peak(const double* values, int count) noexcept
    {
        double value = absolute(values[0]);
        if (value <= 5.0e-8)
        {
            value = 0.0;
            for (int i = 0; i < count; ++i)
                value = std::max(value, absolute(values[i]));
        }
        return value;
    }
    static double integerPower(double base, int exponent) noexcept
    {
        double output = 1.0;
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
    static void gain(double* left, double* right, int count, double target,
                     double& current, double slew, double reciprocal, double mix) noexcept
    {
        double first = (1.0 - mix) * current;
        first = first + target * mix;
        double end = (first - target) * slew;
        end = end + target;
        current = end;
        double step = (end - first) * reciprocal;
        if (absolute(step) > 1.0e-300)
        {
            std::array<double, 2> lanes;
            for (int lane = 0; lane < 2; ++lane)
                lanes[lane] = static_cast<double>(lane + 1) * step + first;
            const double groupStep = 2.0 * step;
            int i = 0;
            for (; i < (count & ~1); i += 2)
            {
                for (int lane = 0; lane < 2; ++lane)
                {
                    const double multiplier = lanes[lane];
                    left[i + lane] = left[i + lane] * multiplier;
                    if (right != nullptr) right[i + lane] = right[i + lane] * multiplier;
                    lanes[lane] = multiplier + groupStep;
                }
            }
            for (; i < count; ++i)
            {
                const double multiplier = static_cast<double>(i + 1) * step + first;
                left[i] = multiplier * left[i];
                if (right != nullptr) right[i] = multiplier * right[i];
            }
        }
        else if (absolute(first - 1.0) > 1.0e-300)
        {
            if (absolute(first) < 1.0e-300)
            {
                std::fill_n(left, count, 0.0);
                if (right != nullptr) std::fill_n(right, count, 0.0);
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
