#include "../Source/DoubleStages.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#define assert(condition) do { if (!(condition)) std::abort(); } while (false)
namespace control
{


static void require(bool condition)
{
    if (!condition)
        std::abort();
}

void test()
{
    using ember::DoubleControlStages;
    std::array<double, 2> input { 8.0, 16.0 }, prefix { 4.0, 8.0 }, state { 0.0, 8.0 };
    std::array<double, 4> output {};
    DoubleControlStages::coefficientRamp(2, 2, input.data(), prefix.data(), state.data(),
                                   0.5, 0.25, output.data(), 0.25);
    require(prefix[0] == 3.0 && prefix[1] == 6.0);
    require(output[0] == 2.0 && output[1] == 10.0);
    require(state[0] == 5.0 && state[1] == 13.0);
    require(output[2] == 0.75 && output[3] == 0.75);

    // Updating an aliased input/output buffer changes the input subsequently
    // read for the state update, which is part of the operation ordering.
    std::array<double, 4> aliased { 8.0, 16.0, 0.0, 0.0 };
    state = { 0.0, 8.0 };
    DoubleControlStages::coefficientRamp(2, 0, aliased.data(), nullptr, state.data(),
                                   0.5, 0.25, aliased.data(), 0.25);
    require(aliased[0] == 2.0 && aliased[1] == 10.0);
    require(state[0] == 2.0 && state[1] == 10.0);
    require(aliased[2] == 0.0 && aliased[3] == 0.0);

    std::array<double, 25> curve {};
    for (int i = 0; i < 25; ++i)
        curve[static_cast<std::size_t>(i)] = static_cast<double>(i) * 0.03125;
    std::array<double, 34 * 2> table {};
    for (int row = 0; row < 34; ++row)
        for (int column = 0; column < 2; ++column)
            table[static_cast<std::size_t>(row * 2 + column)] = static_cast<double>(row * 4 + column);
    DoubleControlStages::interpolateRows(curve.data(), 33, table.data(), 2, output.data(), 0.5);
    require(output[0] == 46.0 && output[1] == 47.0);
    DoubleControlStages::interpolateRows(curve.data(), 33, table.data(), 2, output.data(), 1.0);
    require(output[0] == 92.0 && output[1] == 93.0);

    std::array<double, 9> ramp {};
    double current = 0.0;
    DoubleControlStages::targetRamp(current, ramp.data(), 9, 1.0, 0.25, false);
    require(ramp[0] == 0.25 && ramp[1] == 0.5 && ramp[2] == 0.75);
    require(ramp[3] == 1.0 && ramp[8] == 1.0 && current == 1.0);
    DoubleControlStages::targetRamp(current, ramp.data(), 5, 0.0, 0.25, false);
    require(ramp[0] == 0.75 && ramp[3] == 0.0 && ramp[4] == 0.0 && current == 0.0);
    DoubleControlStages::targetRamp(current, nullptr, 0, 3.0, 1.0, true);
    require(current == 3.0);
    DoubleControlStages::targetRamp(current, nullptr, -1, 7.0, 1.0, false);
    require(current == 3.0);
    DoubleControlStages::targetRamp(current, ramp.data(), 9, -0.0, 0.1, true);
    require(std::bit_cast<std::uint64_t>(current) == 0x8000000000000000ull);
}
}

namespace filter
{


namespace
{
void require(bool condition)
{
    if (!condition)
        std::abort();
}

bool same(double a, double b)
{
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
}

void test()
{
    using ember::DoubleFilterStages;
    // A single recursive pole has a hand-derived decaying impulse response.
    std::array<double, 5> pole { 0.5, 0.0, 1.0, 0.0, 0.0 };
    std::array<double, 4> state {};
    std::array<double, 3> left { 1.0, 0.0, 0.0 }, right { -1.0, 0.0, 0.0 };
    DoubleFilterStages::cascade(left.data(), right.data(), 3, pole.data(), state.data(), 1);
    require(left == std::array<double, 3> { 1.0, 0.5, 0.25 });
    require(right == std::array<double, 3> { -1.0, -0.5, -0.25 });
    require(state == std::array<double, 4> { 0.25, -0.25, 0.5, -0.5 });

    // Two cascaded unit delays emit the impulse on the third sample.
    std::array<double, 10> delays { 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    std::array<double, 8> delayState {};
    left = { 1, 0, 0 };
    right = { 0, 1, 0 };
    DoubleFilterStages::cascade(left.data(), right.data(), 3, delays.data(), delayState.data(), 2);
    require(left == std::array<double, 3> { 0, 0, 1 });
    require(right == std::array<double, 3> { 0, 0, 0 });

    // The accumulating realization retains its second accumulator each sample.
    std::array<double, 10> coefficients { 0.5, 0, 0, -1, -0.25, 0, 0, 0, 0, 0 };
    state.fill(0);
    left = { 1, 0, 0 };
    right = { -1, 0, 0 };
    DoubleFilterStages::accumulating(left.data(), right.data(), 3, coefficients.data(), state.data(), false);
    require(left == std::array<double, 3> { 0.5, -0.5, -0.125 });
    require(right == std::array<double, 3> { -0.5, 0.5, 0.125 });
    require(state == std::array<double, 4> { 0, 0, 0.03125, -0.03125 });

    // A ramp advances before processing, and does not mutate its initial values.
    coefficients = { 1, 0, 0, 0, 0, 0.5, 0, 0, 0, 0 };
    const auto savedCoefficients = coefficients;
    state.fill(0);
    left.fill(1);
    right.fill(-1);
    DoubleFilterStages::accumulating(left.data(), right.data(), 3, coefficients.data(), state.data(), true);
    require(left == std::array<double, 3> { 1.5, 2.0, 2.5 });
    require(right == std::array<double, 3> { -1.5, -2.0, -2.5 });
    require(coefficients == savedCoefficients);

    // Cleanup has a strict threshold, runs on empty blocks, and clears -0 bits.
    const double threshold = 1.0e-300;
    const double below = std::nextafter(threshold, 0.0);
    state = { threshold, -threshold, below, -0.0 };
    DoubleFilterStages::cascade(nullptr, nullptr, 0, pole.data(), state.data(), 1);
    require(same(state[0], threshold) && same(state[1], -threshold));
    require(same(state[2], 0.0) && same(state[3], 0.0));
    state = { below, -below, threshold, -threshold };
    DoubleFilterStages::accumulating(nullptr, nullptr, 0, coefficients.data(), state.data(), false);
    require(same(state[0], 0.0) && same(state[1], 0.0));
    require(same(state[2], threshold) && same(state[3], -threshold));
    DoubleFilterStages::cascade(nullptr, nullptr, 3, nullptr, nullptr, 0);

    // Static coefficients preserve exact state/output across block partitions
    // when intermediate state stays above the call-end cleanup threshold.
    std::array<double, 8> wholeLeft { 1, -0.5, 0.25, 0, 0.75, -1, 0, 0.125 };
    std::array<double, 8> wholeRight { -1, 0.125, 0, 0.5, 0, 1, 0.25, -0.5 };
    auto splitLeft = wholeLeft, splitRight = wholeRight;
    std::array<double, 4> wholeState {}, splitState {};
    DoubleFilterStages::cascade(wholeLeft.data(), wholeRight.data(), 8, pole.data(), wholeState.data(), 1);
    DoubleFilterStages::cascade(splitLeft.data(), splitRight.data(), 3, pole.data(), splitState.data(), 1);
    DoubleFilterStages::cascade(splitLeft.data() + 3, splitRight.data() + 3, 5, pole.data(), splitState.data(), 1);
    require(std::memcmp(wholeLeft.data(), splitLeft.data(), sizeof(wholeLeft)) == 0);
    require(std::memcmp(wholeRight.data(), splitRight.data(), sizeof(wholeRight)) == 0);
    require(std::memcmp(wholeState.data(), splitState.data(), sizeof(wholeState)) == 0);

    coefficients = { 0.5, 0, 0, -1, -0.25, 0, 0, 0, 0, 0 };
    wholeLeft = splitLeft = { 1, -0.5, 0.25, 0, 0.75, -1, 0, 0.125 };
    wholeRight = splitRight = { -1, 0.125, 0, 0.5, 0, 1, 0.25, -0.5 };
    wholeState.fill(0);
    splitState.fill(0);
    DoubleFilterStages::accumulating(wholeLeft.data(), wholeRight.data(), 8, coefficients.data(), wholeState.data(), false);
    DoubleFilterStages::accumulating(splitLeft.data(), splitRight.data(), 1, coefficients.data(), splitState.data(), false);
    DoubleFilterStages::accumulating(splitLeft.data() + 1, splitRight.data() + 1, 7, coefficients.data(), splitState.data(), false);
    require(std::memcmp(wholeLeft.data(), splitLeft.data(), sizeof(wholeLeft)) == 0);
    require(std::memcmp(wholeRight.data(), splitRight.data(), sizeof(wholeRight)) == 0);
    require(std::memcmp(wholeState.data(), splitState.data(), sizeof(wholeState)) == 0);
}
}

namespace lookup
{


// Keep checks enabled when this standalone test is built in Release mode.

void test()
{
    using ember::DoubleLookupStage;
    assert(DoubleLookupStage::amplitudeCoordinate(0.0).row == 23);
    assert(DoubleLookupStage::amplitudeCoordinate(0.0).fraction == 0.0);
    assert(DoubleLookupStage::amplitudeCoordinate(0.5).row == 1);
    assert(DoubleLookupStage::amplitudeCoordinate(0.5).fraction == 0.0);
    assert(DoubleLookupStage::amplitudeCoordinate(0.75).row == 1);
    assert(DoubleLookupStage::amplitudeCoordinate(0.75).fraction == 0.5);
    assert(DoubleLookupStage::amplitudeCoordinate(0.25).row == 2);
    assert(DoubleLookupStage::amplitudeCoordinate(0x1p-23).row == 23);
    assert(DoubleLookupStage::amplitudeCoordinate(0x1p-23).fraction == 0.5);
    assert(DoubleLookupStage::amplitudeCoordinate(0x1p-24).fraction == 0.25);

    std::array<double, 25 * 65> table {};
    table.fill(7.0);
    assert(DoubleLookupStage::interpolate(table.data() + 32, 3 * 65 - 7, 0.3, 0.6) == 7.0);
    for (int row = 0; row < 25; ++row)
        for (int column = 0; column < 65; ++column)
            table[static_cast<std::size_t>(row * 65 + column)] = 2.0 * row + column - 32;
    // The amplitude fraction interpolates from the next row toward the current row.
    assert(DoubleLookupStage::interpolate(table.data() + 32, 3 * 65 - 7, 0.25, 0.5) == 0.25);

    double envelope = 0.0;
    std::array<double, 3> samples { -1.0, 0.0, 1.0 };
    std::array<double, 3> scratch {};
    std::array<int, 3> indices {};
    table.fill(7.0);
    DoubleLookupStage::process(samples.data(), 3, 0.0, envelope, table.data() + 32,
                         scratch.data(), indices.data(), 1.0, 1);
    assert(samples[0] == 7.0 && samples[1] == 7.0 && samples[2] == 7.0);
    assert(envelope == 1.0);
    assert(indices[1] == 23 * 65);
    const double saved = envelope;
    DoubleLookupStage::process(nullptr, 0, 0.0, envelope, nullptr, nullptr, nullptr, 0.0, 1);
    assert(envelope == saved);

    // At the signed signal endpoints the negative integral column deliberately
    // uses the preceding cell with interpolation fraction one.
    samples = { -1.0, 0.0, 1.0 };
    envelope = 0.0;
    DoubleLookupStage::process(samples.data(), 3, 1.0, envelope, table.data() + 32,
                         scratch.data(), indices.data(), 1.0, 1);
    assert(indices[0] == 65 - 32);
    assert(indices[2] == 65 + 31);
    assert(samples[0] == 7.0 && samples[2] == 7.0);
}
}

int main() { control::test(); filter::test(); lookup::test(); }
