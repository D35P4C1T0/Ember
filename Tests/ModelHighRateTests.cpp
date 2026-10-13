#include "../Source/ModelHighRate.h"
#include "../Source/ModelTables.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
struct Random
{
    std::uint32_t state = 101010u;
    std::uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float unit() noexcept { return static_cast<float>(next() >> 8) * 0x1p-24f; }
};
void hashFloat(std::uint64_t& hash, float value)
{
    std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (int i = 0; i < 4; ++i)
    {
        hash ^= (bits >> (i * 8)) & 255u;
        hash *= 1099511628211ull;
    }
}
}

int checkGroup(unsigned group, bool routing)
{
    using ember::ModelHighRate;
    const auto* data = ember::floatModelTables(group);
    if (data == nullptr) return 1;
    ModelHighRate::State state;
    std::copy((*data)[1].begin(), (*data)[1].end(), state.audio.begin());
    std::copy((*data)[3].begin(), (*data)[3].end(), state.tracking.begin());
    const unsigned derivedSlot = (*data)[4].size() == 27 ? 4 : 5;
    const unsigned meterSlot = derivedSlot == 4 ? 5 : 4;
    std::copy((*data)[derivedSlot].begin(), (*data)[derivedSlot].end(), state.derived.begin());
    std::copy((*data)[meterSlot].begin(), (*data)[meterSlot].end(), state.meters.begin());
    std::copy((*data)[7].begin(), (*data)[7].end(), state.filters.begin());
    const ModelHighRate::Tables tables { (*data)[0].data(), (*data)[2].data(), (*data)[6].data() };
    const auto initialState = state;
    float sentinelLeft = 7.0f, sentinelRight = -7.0f;
    ModelHighRate::processStereo(state, tables, nullptr, nullptr,
        &sentinelLeft, &sentinelRight, 0, 1.0f, group);
    ModelHighRate::processStereo(state, tables, nullptr, nullptr,
        &sentinelLeft, &sentinelRight, ModelHighRate::maximumFrames(group) + 1, 1.0f, group);
    ModelHighRate::processMono(state, tables, nullptr, &sentinelLeft, 1, 1.0f, 6);
    if (sentinelLeft != 7.0f || sentinelRight != -7.0f
        || std::memcmp(&state, &initialState, sizeof(state)) != 0) return 1;
    Random random;
    std::array<float, 1024> left, right, outputLeft, outputRight;
    outputRight.fill(0.0f);
    std::uint64_t hash = 14695981039346656037ull;
    std::uint64_t samples = 0;
    // Deterministic automation exercises endpoints, all block sizes, bypass,
    // coefficient interpolation and a long silence that reaches the idle gate.
    for (int trial = 0; trial < 12000; ++trial)
    {
        const int count = 1 + static_cast<int>(random.next() % ModelHighRate::maximumFrames(group));
        const bool immediate = trial == 0 || (trial < 1000 && trial % 271 == 0);
        const bool blockSlew = trial < 1000 && trial % 3 == 1;
        const float amount = trial < 1000 ? (trial % 7 == 0 ? 0.0f : trial % 7 == 1 ? 1.0f : random.unit()) : 0.5f;
        const float mode = trial < 1000 ? static_cast<float>(random.next() % 3) * 0.5f : 0.0f;
        const float bypass = trial < 1000 && trial % 137 > 109 ? 1.0f : 0.0f;
        const float outputGain = 0.5f + random.unit();
        const float rate = group == 2 ? 88200.0f : group == 3 ? 96000.0f : group == 4 ? 176400.0f : 192000.0f;
        ModelHighRate::Controls controls { amount, mode, rate, bypass, 0.0f };
        ModelHighRate::update(state, tables, controls, count, immediate, blockSlew, group);
        hashFloat(hash, controls.auxiliary);
        hashFloat(hash, controls.envelope);
        for (int i = 0; i < count; ++i)
        {
            left[i] = trial > 1000 || trial % 199 > 150 ? 0.0f : (random.unit() - 0.5f) * 0.8f;
            right[i] = trial > 1000 || trial % 199 > 150 ? 0.0f : (random.unit() - 0.5f) * 0.8f;
        }
        if (routing)
            ModelHighRate::processChannels(state, tables, left.data(), right.data(), outputLeft.data(), outputRight.data(), count, outputGain, (trial & 1) != 0, (trial & 2) != 0, group);
        else
            ModelHighRate::processStereo(state, tables, left.data(), right.data(), outputLeft.data(), outputRight.data(), count, outputGain, group);
        for (int i = 0; i < count; ++i)
        {
            hashFloat(hash, outputLeft[i]);
            if (!routing || (trial & 2) == 0) hashFloat(hash, outputRight[i]);
        }
        for (float value : state.audio) hashFloat(hash, value);
        for (float value : state.tracking) hashFloat(hash, value);
        for (float value : state.derived) hashFloat(hash, value);
        for (float value : state.meters) hashFloat(hash, value);
        for (float value : state.filters) hashFloat(hash, value);
        samples += static_cast<unsigned>(count);
    }
    // Regression digest checks deterministic output and state across architectures.
    constexpr std::array<std::uint64_t, 4> stereoExpected { 0x15f59d9a26c6ceb5ull, 0xd8be43ee146b6745ull, 0x6017be5858bef2ccull, 0xa9553481c3db3016ull };
    constexpr std::array<std::uint64_t, 4> routingExpected { 0xd7f6c722bd19120bull, 0x7114b074e299fc0eull, 0xe590b94e7258b679ull, 0x2dc0516a74f36df9ull };
    const auto& expected = routing ? routingExpected : stereoExpected;
    std::printf("ModelHighRate group %u %s: %llu frames, digest %016llx\n", group, routing ? "routing" : "stereo",
                static_cast<unsigned long long>(samples), static_cast<unsigned long long>(hash));
    return hash == expected[group - 2] ? 0 : 1;
}

int main()
{
    for (unsigned group = 2; group < 6; ++group)
        if (checkGroup(group, false) != 0 || checkGroup(group, true) != 0) return 1;
    return 0;
}
