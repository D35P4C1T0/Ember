#include "../Source/LookupStage.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>

// Keep checks enabled when this standalone test is built in Release mode.
#undef assert
#define assert(condition) do { if (!(condition)) std::abort(); } while (false)

int main()
{
    using ember::LookupStage;
    assert(LookupStage::amplitudeCoordinate(0.0f).row == 23);
    assert(LookupStage::amplitudeCoordinate(0.0f).fraction == 0.0f);
    assert(LookupStage::amplitudeCoordinate(0.5f).row == 1);
    assert(LookupStage::amplitudeCoordinate(0.5f).fraction == 0.0f);
    assert(LookupStage::amplitudeCoordinate(0.75f).row == 1);
    assert(LookupStage::amplitudeCoordinate(0.75f).fraction == 0.5f);
    assert(LookupStage::amplitudeCoordinate(0.25f).row == 2);
    assert(LookupStage::amplitudeCoordinate(0x1p-23f).row == 23);
    assert(LookupStage::amplitudeCoordinate(0x1p-23f).fraction == 0.5f);
    assert(LookupStage::amplitudeCoordinate(0x1p-24f).fraction == 0.25f);

    std::array<float, 25 * 65> table {};
    table.fill(7.0f);
    assert(LookupStage::interpolate(table.data() + 32, 3 * 65 - 7, 0.3f, 0.6f) == 7.0f);
    for (int row = 0; row < 25; ++row)
        for (int column = 0; column < 65; ++column)
            table[static_cast<std::size_t>(row * 65 + column)] = 2.0f * row + column - 32;
    // The amplitude fraction interpolates from the next row toward the current row.
    assert(LookupStage::interpolate(table.data() + 32, 3 * 65 - 7, 0.25f, 0.5f) == 0.25f);

    float envelope = 0.0f;
    std::array<float, 3> samples { -1.0f, 0.0f, 1.0f };
    std::array<float, 3> scratch {};
    std::array<int, 3> indices {};
    table.fill(7.0f);
    LookupStage::process(samples.data(), 3, 0.0f, envelope, table.data() + 32,
                         scratch.data(), indices.data(), 1.0f, 1);
    assert(samples[0] == 7.0f && samples[1] == 7.0f && samples[2] == 7.0f);
    assert(envelope == 1.0f);
    assert(indices[1] == 23 * 65);
    const float saved = envelope;
    LookupStage::process(nullptr, 0, 0.0f, envelope, nullptr, nullptr, nullptr, 0.0f, 1);
    assert(envelope == saved);

    // At the signed signal endpoints the negative integral column deliberately
    // uses the preceding cell with interpolation fraction one.
    samples = { -1.0f, 0.0f, 1.0f };
    envelope = 0.0f;
    LookupStage::process(samples.data(), 3, 1.0f, envelope, table.data() + 32,
                         scratch.data(), indices.data(), 1.0f, 1);
    assert(indices[0] == 65 - 32);
    assert(indices[2] == 65 + 31);
    assert(samples[0] == 7.0f && samples[2] == 7.0f);
}
