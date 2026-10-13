#include "../Source/ControlStages.h"

#include <array>
#include <cstdlib>

static void require(bool condition)
{
    if (!condition)
        std::abort();
}

int main()
{
    using ember::ControlStages;
    std::array<float, 2> input { 8.0f, 16.0f }, prefix { 4.0f, 8.0f }, state { 0.0f, 8.0f };
    std::array<float, 4> output {};
    ControlStages::coefficientRamp(2, 2, input.data(), prefix.data(), state.data(),
                                   0.5f, 0.25f, output.data(), 0.25f);
    require(prefix[0] == 3.0f && prefix[1] == 6.0f);
    require(output[0] == 2.0f && output[1] == 10.0f);
    require(state[0] == 5.0f && state[1] == 13.0f);
    require(output[2] == 0.75f && output[3] == 0.75f);

    // Updating an aliased input/output buffer changes the input subsequently
    // read for the state update, which is part of the operation ordering.
    std::array<float, 4> aliased { 8.0f, 16.0f, 0.0f, 0.0f };
    state = { 0.0f, 8.0f };
    ControlStages::coefficientRamp(2, 0, aliased.data(), nullptr, state.data(),
                                   0.5f, 0.25f, aliased.data(), 0.25f);
    require(aliased[0] == 2.0f && aliased[1] == 10.0f);
    require(state[0] == 2.0f && state[1] == 10.0f);
    require(aliased[2] == 0.0f && aliased[3] == 0.0f);

    std::array<float, 25> curve {};
    for (int i = 0; i < 25; ++i)
        curve[static_cast<std::size_t>(i)] = static_cast<float>(i) * 0.03125f;
    std::array<float, 34 * 2> table {};
    for (int row = 0; row < 34; ++row)
        for (int column = 0; column < 2; ++column)
            table[static_cast<std::size_t>(row * 2 + column)] = static_cast<float>(row * 4 + column);
    ControlStages::interpolateRows(curve.data(), 33, table.data(), 2, output.data(), 0.5f);
    require(output[0] == 46.0f && output[1] == 47.0f);
    ControlStages::interpolateRows(curve.data(), 33, table.data(), 2, output.data(), 1.0f);
    require(output[0] == 92.0f && output[1] == 93.0f);

    std::array<float, 9> ramp {};
    float current = 0.0f;
    ControlStages::targetRamp(current, ramp.data(), 9, 1.0f, 0.25f, false);
    require(ramp[0] == 0.25f && ramp[1] == 0.5f && ramp[2] == 0.75f);
    require(ramp[3] == 1.0f && ramp[8] == 1.0f && current == 1.0f);
    ControlStages::targetRamp(current, ramp.data(), 5, 0.0f, 0.25f, false);
    require(ramp[0] == 0.75f && ramp[3] == 0.0f && ramp[4] == 0.0f && current == 0.0f);
    ControlStages::targetRamp(current, nullptr, 0, 3.0f, 1.0f, true);
    require(current == 3.0f);
    ControlStages::targetRamp(current, nullptr, -1, 7.0f, 1.0f, false);
    require(current == 3.0f);
    ControlStages::targetRamp(current, ramp.data(), 9, -0.0f, 0.1f, true);
    require(std::bit_cast<std::uint32_t>(current) == 0x80000000u);
}
