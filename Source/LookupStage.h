#pragma once

#include <bit>
#include <cstdint>

namespace ember
{
// The table pointer addresses the centre column of the first row.
// Callers provide scratch storage and a table with 25 rows of 65 floats.
// Audio, gain, and state must be finite; the envelope must be nonnegative.
class LookupStage
{
public:
    struct Coordinate
    {
        int row;
        float fraction;
    };

    static Coordinate amplitudeCoordinate(float amplitude) noexcept
    {
        if (amplitude == 0.0f)
            return { 23, 0.0f };

        const auto bits = std::bit_cast<std::uint32_t>(amplitude);
        const int exponent = static_cast<int>((bits >> 23) & 255u) - 126;
        int row = 1 - exponent;
        float fraction = std::bit_cast<float>((bits & 0xbf7fffffu) | 0x3f000000u);
        if (row > 22)
        {
            auto mantissaBits = std::bit_cast<std::uint32_t>(fraction);
            auto shifted = (static_cast<std::uint32_t>(23 - row) << 23)
                         + (mantissaBits & 0xff800000u);
            shifted = ((shifted ^ mantissaBits) & 0x7f800000u) ^ mantissaBits;
            fraction = std::bit_cast<float>(shifted);
            row = 23;
        }
        else
        {
            fraction = fraction - 0.5f;
            fraction = fraction * 2.0f;
        }
        return { row, fraction };
    }

    // Separate float operations are intentional; compile with contraction disabled.
    static float interpolate(const float* centeredTable, int index,
                             float signalFraction, float amplitudeFraction) noexcept
    {
        float next = centeredTable[index + 66] - centeredTable[index + 65];
        next = next * signalFraction;
        float current = centeredTable[index + 1] - centeredTable[index];
        next = next + centeredTable[index + 65];
        current = current * signalFraction;
        current = current + centeredTable[index];
        current = current - next;
        current = current * amplitudeFraction;
        return current + next;
    }

    static void process(float* samples, int count, float release, float& envelope,
                        const float* centeredTable, float* scratch, int* indices,
                        float envelopeGain, int multiplier) noexcept
    {
        if (count <= 0)
            return;
        for (int i = 0; i < count; ++i)
            scratch[i] = absolute(samples[i]);

        float previous = envelope;
        for (int i = 0; i < count; ++i)
        {
            float decayed = previous - scratch[i];
            decayed = decayed * release;
            decayed = scratch[i] + decayed;
            const float scaled = samples[i] * 0x1.6a09e6p-1f;
            const float magnitude = absolute(scaled);
            previous = magnitude > decayed ? magnitude : decayed;
            previous = previous > 1.0e-30f ? previous : 1.0e-30f;
            samples[i] = scaled;
            scratch[i] = previous;
        }
        envelope = previous;

        const float multiplierFloat = static_cast<float>(multiplier);
        for (int i = 0; i < count; ++i)
        {
            samples[i] = samples[i] / scratch[i];
            float amplitude = scratch[i] * envelopeGain;
            amplitude = amplitude * multiplierFloat;
            amplitude = amplitude > -0x1.fffffcp-1f ? amplitude : -0x1.fffffcp-1f;
            amplitude = amplitude < 0x1.fffffcp-1f ? amplitude : 0x1.fffffcp-1f;
            scratch[i] = amplitude;
        }
        for (int i = 0; i < count; ++i)
        {
            const auto coordinate = amplitudeCoordinate(scratch[i]);
            const float position = 31.0f * samples[i];
            int column = static_cast<int>(position);
            if (position < 0.0f)
                --column;
            indices[i] = coordinate.row * 65 + column;
            scratch[i] = coordinate.fraction;
            samples[i] = position - static_cast<float>(column);
        }
        for (int i = 0; i < count; ++i)
            samples[i] = interpolate(centeredTable, indices[i], samples[i], scratch[i]);
    }

private:
    static float absolute(float value) noexcept
    {
        return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
    }
};
}
