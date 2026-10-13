#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace ember
{
// Callers supply finite values, valid table guard rows, and writable buffers.
// Float contraction must be disabled to preserve these operation boundaries.
class ControlStages
{
public:
    static void coefficientRamp(int count, int prefixCount, const float* input,
                                float* prefix, float* state, float slew,
                                float residualGain, float* output, float mix) noexcept
    {
        const float complement = 1.0f - mix;
        for (int i = 0; i < prefixCount; ++i)
            prefix[i] = complement * prefix[i];
        for (int i = 0; i < count; ++i)
        {
            float value = complement * state[i];
            float contribution = input[i] * mix;
            value = value + contribution;
            output[i] = value;
            float end = value - input[i];
            end = end * slew;
            end = end + input[i];
            state[i] = end;
            end = end - value;
            output[count + i] = end * residualGain;
        }
    }

    // The curve uses 23 intervals. Both curve and coefficient tables must
    // include the next element/row even when an interpolation fraction is zero.
    static void interpolateRows(const float* curve, int rows, const float* table,
                                int width, float* output, float mix) noexcept
    {
        float position = mix * 23.0f;
        int curveIndex = static_cast<int>(position);
        if (position < 0.0f)
            --curveIndex;
        float fraction = position - static_cast<float>(curveIndex);
        float mapped = curve[curveIndex + 1] - curve[curveIndex];
        mapped = mapped * fraction;
        mapped = mapped + curve[curveIndex];
        position = mapped * static_cast<float>(rows - 1);
        int row = static_cast<int>(position);
        if (position < 0.0f)
            --row;
        fraction = position - static_cast<float>(row);
        for (int i = 0; i < width; ++i)
        {
            const int index = row * width + i;
            float value = table[index + width] - table[index];
            value = value * fraction;
            output[i] = value + table[index];
        }
    }

    static void targetRamp(float& current, float* output, int count, float target,
                           float step, bool immediate) noexcept
    {
        if (immediate)
        {
            for (int i = 0; i < count; ++i)
                output[i] = target;
            current = target;
            return;
        }
        if (count <= 0)
            return;
        const bool descending = current >= target;
        if (descending)
            step = std::bit_cast<float>(std::bit_cast<std::uint32_t>(step) ^ 0x80000000u);
        const float first = current + step;
        std::array<float, 4> lanes;
        for (int lane = 0; lane < 4; ++lane)
        {
            float value = static_cast<float>(lane) * step;
            lanes[static_cast<std::size_t>(lane)] = value + first;
        }
        const float groupStep = 4.0f * step;
        const int groupedCount = count & ~3;
        int i = 0;
        for (; i < groupedCount; i += 4)
        {
            for (int lane = 0; lane < 4; ++lane)
            {
                const float value = lanes[static_cast<std::size_t>(lane)];
                lanes[static_cast<std::size_t>(lane)] = value + groupStep;
                output[i + lane] = descending ? (value > target ? value : target)
                                             : (value < target ? value : target);
            }
        }
        for (; i < count; ++i)
        {
            float value = static_cast<float>(i) * step;
            value = value + first;
            output[i] = descending ? (value > target ? value : target)
                                   : (value < target ? value : target);
        }
        current = output[count - 1];
    }
};
}
