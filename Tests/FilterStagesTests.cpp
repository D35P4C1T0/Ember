#include "../Source/FilterStages.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
void require(bool condition)
{
    if (!condition)
        std::abort();
}

bool same(float a, float b)
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}
}

int main()
{
    using ember::FilterStages;
    // A single recursive pole has a hand-derived decaying impulse response.
    std::array<float, 5> pole { 0.5f, 0.0f, 1.0f, 0.0f, 0.0f };
    std::array<float, 4> state {};
    std::array<float, 3> left { 1.0f, 0.0f, 0.0f }, right { -1.0f, 0.0f, 0.0f };
    FilterStages::cascade(left.data(), right.data(), 3, pole.data(), state.data(), 1);
    require(left == std::array<float, 3> { 1.0f, 0.5f, 0.25f });
    require(right == std::array<float, 3> { -1.0f, -0.5f, -0.25f });
    require(state == std::array<float, 4> { 0.25f, -0.25f, 0.5f, -0.5f });

    // Two cascaded unit delays emit the impulse on the third sample.
    std::array<float, 10> delays { 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    std::array<float, 8> delayState {};
    left = { 1, 0, 0 };
    right = { 0, 1, 0 };
    FilterStages::cascade(left.data(), right.data(), 3, delays.data(), delayState.data(), 2);
    require(left == std::array<float, 3> { 0, 0, 1 });
    require(right == std::array<float, 3> { 0, 0, 0 });

    // The accumulating realization retains its second accumulator each sample.
    std::array<float, 10> coefficients { 0.5f, 0, 0, -1, -0.25f, 0, 0, 0, 0, 0 };
    state.fill(0);
    left = { 1, 0, 0 };
    right = { -1, 0, 0 };
    FilterStages::accumulating(left.data(), right.data(), 3, coefficients.data(), state.data(), false);
    require(left == std::array<float, 3> { 0.5f, -0.5f, -0.125f });
    require(right == std::array<float, 3> { -0.5f, 0.5f, 0.125f });
    require(state == std::array<float, 4> { 0, 0, 0.03125f, -0.03125f });

    // A ramp advances before processing, and does not mutate its initial values.
    coefficients = { 1, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0 };
    const auto savedCoefficients = coefficients;
    state.fill(0);
    left.fill(1);
    right.fill(-1);
    FilterStages::accumulating(left.data(), right.data(), 3, coefficients.data(), state.data(), true);
    require(left == std::array<float, 3> { 1.5f, 2.0f, 2.5f });
    require(right == std::array<float, 3> { -1.5f, -2.0f, -2.5f });
    require(coefficients == savedCoefficients);

    // Cleanup has a strict threshold, runs on empty blocks, and clears -0 bits.
    const float threshold = 1.0e-30f;
    const float below = std::nextafter(threshold, 0.0f);
    state = { threshold, -threshold, below, -0.0f };
    FilterStages::cascade(nullptr, nullptr, 0, pole.data(), state.data(), 1);
    require(same(state[0], threshold) && same(state[1], -threshold));
    require(same(state[2], 0.0f) && same(state[3], 0.0f));
    state = { below, -below, threshold, -threshold };
    FilterStages::accumulating(nullptr, nullptr, 0, coefficients.data(), state.data(), false);
    require(same(state[0], 0.0f) && same(state[1], 0.0f));
    require(same(state[2], threshold) && same(state[3], -threshold));
    FilterStages::cascade(nullptr, nullptr, 3, nullptr, nullptr, 0);

    // Static coefficients preserve exact state/output across block partitions
    // when intermediate state stays above the call-end cleanup threshold.
    std::array<float, 8> wholeLeft { 1, -0.5f, 0.25f, 0, 0.75f, -1, 0, 0.125f };
    std::array<float, 8> wholeRight { -1, 0.125f, 0, 0.5f, 0, 1, 0.25f, -0.5f };
    auto splitLeft = wholeLeft, splitRight = wholeRight;
    std::array<float, 4> wholeState {}, splitState {};
    FilterStages::cascade(wholeLeft.data(), wholeRight.data(), 8, pole.data(), wholeState.data(), 1);
    FilterStages::cascade(splitLeft.data(), splitRight.data(), 3, pole.data(), splitState.data(), 1);
    FilterStages::cascade(splitLeft.data() + 3, splitRight.data() + 3, 5, pole.data(), splitState.data(), 1);
    require(std::memcmp(wholeLeft.data(), splitLeft.data(), sizeof(wholeLeft)) == 0);
    require(std::memcmp(wholeRight.data(), splitRight.data(), sizeof(wholeRight)) == 0);
    require(std::memcmp(wholeState.data(), splitState.data(), sizeof(wholeState)) == 0);

    coefficients = { 0.5f, 0, 0, -1, -0.25f, 0, 0, 0, 0, 0 };
    wholeLeft = splitLeft = { 1, -0.5f, 0.25f, 0, 0.75f, -1, 0, 0.125f };
    wholeRight = splitRight = { -1, 0.125f, 0, 0.5f, 0, 1, 0.25f, -0.5f };
    wholeState.fill(0);
    splitState.fill(0);
    FilterStages::accumulating(wholeLeft.data(), wholeRight.data(), 8, coefficients.data(), wholeState.data(), false);
    FilterStages::accumulating(splitLeft.data(), splitRight.data(), 1, coefficients.data(), splitState.data(), false);
    FilterStages::accumulating(splitLeft.data() + 1, splitRight.data() + 1, 7, coefficients.data(), splitState.data(), false);
    require(std::memcmp(wholeLeft.data(), splitLeft.data(), sizeof(wholeLeft)) == 0);
    require(std::memcmp(wholeRight.data(), splitRight.data(), sizeof(wholeRight)) == 0);
    require(std::memcmp(wholeState.data(), splitState.data(), sizeof(wholeState)) == 0);
}
