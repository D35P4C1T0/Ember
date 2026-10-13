#include "../Source/ModelDoubleHighRate.h"
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
template <typename T>
void hashFloat(std::uint64_t& hash, T value)
{
    const auto bytes = std::bit_cast<std::array<unsigned char, sizeof(T)>>(value);
    for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }

}
}

int checkGroup(unsigned group, bool routing)
{
    using ember::ModelDoubleHighRate;
    const auto* data = ember::doubleModelTables(group);
    if (data == nullptr) return 1;
    ModelDoubleHighRate::State state;
    std::copy((*data)[1].begin(), (*data)[1].end(), state.audio.begin());
    std::copy((*data)[3].begin(), (*data)[3].end(), state.tracking.begin());
    const unsigned derivedSlot = (*data)[4].size() == 27 ? 4 : 5;
    const unsigned meterSlot = derivedSlot == 4 ? 5 : 4;
    std::copy((*data)[derivedSlot].begin(), (*data)[derivedSlot].end(), state.derived.begin());
    std::copy((*data)[meterSlot].begin(), (*data)[meterSlot].end(), state.meters.begin());
    std::copy((*data)[7].begin(), (*data)[7].end(), state.filters.begin());
    const ModelDoubleHighRate::Tables tables { (*data)[0].data(), (*data)[2].data(), (*data)[6].data() };
    const auto initialState = state;
    double sentinelLeft = 7.0f, sentinelRight = -7.0f;
    ModelDoubleHighRate::processStereoDouble(state, tables, nullptr, nullptr,
        &sentinelLeft, &sentinelRight, 0, 1.0f, group);
    ModelDoubleHighRate::processStereoDouble(state, tables, nullptr, nullptr,
        &sentinelLeft, &sentinelRight, ModelDoubleHighRate::maximumFrames(group) + 1, 1.0f, group);
    ModelDoubleHighRate::processMonoDouble(state, tables, nullptr, &sentinelLeft, 1, 1.0f, 6);
    if (sentinelLeft != 7.0f || sentinelRight != -7.0f
        || std::memcmp(&state, &initialState, sizeof(state)) != 0) return 1;
    Random random;
    std::array<double, 1024> left{}, right{}, outputLeft{}, outputRight{};
    outputRight.fill(0.0f);
    std::uint64_t hash = 14695981039346656037ull;
    std::uint64_t samples = 0;
    // Deterministic automation exercises endpoints, all block sizes, bypass,
    // coefficient interpolation and a long silence that reaches the idle gate.
    for (int trial = 0; trial < 12000; ++trial)
    {
        const int count = 1 + static_cast<int>(random.next() % ModelDoubleHighRate::maximumFrames(group));
        const bool immediate = trial == 0 || (trial < 1000 && trial % 271 == 0);
        const bool blockSlew = trial < 1000 && trial % 3 == 1;
        const float amount = trial < 1000 ? (trial % 7 == 0 ? 0.0f : trial % 7 == 1 ? 1.0f : random.unit()) : 0.5f;
        const float mode = trial < 1000 ? static_cast<float>(random.next() % 3) * 0.5f : 0.0f;
        const float bypass = trial < 1000 && trial % 137 > 109 ? 1.0f : 0.0f;
        const float outputGain = 0.5f + random.unit();
        const float rate = group == 2 ? 88200.0f : group == 3 ? 96000.0f : group == 4 ? 176400.0f : 192000.0f;
        ModelDoubleHighRate::Controls controls { amount, mode, rate, bypass, 0.0f };
        ModelDoubleHighRate::update(state, tables, controls, count, immediate, blockSlew, group);
        hashFloat(hash, controls.auxiliary);
        hashFloat(hash, controls.envelope);
        for (int i = 0; i < count; ++i)
        {
            left[i] = trial > 1000 || trial % 199 > 150 ? 0.0f : (random.unit() - 0.5f) * 0.8f;
            right[i] = trial > 1000 || trial % 199 > 150 ? 0.0f : (random.unit() - 0.5f) * 0.8f;
        }
        if (trial < 1000 && trial % 5 == 1)
            for (int i = 0; i < count; ++i)
            {
                left[i] += static_cast<double>(random.unit()) * 0x1p-45;
                right[i] -= static_cast<double>(random.unit()) * 0x1p-46;
            }
        if (trial < 1000 && trial % 31 == 0)
            for (int i = 0; i < count; ++i)
            {
                left[i] = (i & 1) ? -0.0f : 0.0f;
                right[i] = (i & 1) ? 0x1p-120f : -0x1p-120f;
            }
        if (routing)
            ModelDoubleHighRate::processChannelsDouble(state, tables, left.data(), right.data(), outputLeft.data(), outputRight.data(), count, outputGain, (trial & 1) != 0, (trial & 2) != 0, group);
        else
            ModelDoubleHighRate::processStereoDouble(state, tables, left.data(), right.data(), outputLeft.data(), outputRight.data(), count, outputGain, group);
        for (int i = 0; i < count; ++i)
        {
            hashFloat(hash, outputLeft[i]);
            hashFloat(hash, outputRight[i]);
        }
        for (double value : state.audio) hashFloat(hash, value);
        for (double value : state.tracking) hashFloat(hash, value);
        for (double value : state.derived) hashFloat(hash, value);
        for (double value : state.meters) hashFloat(hash, value);
        for (double value : state.filters) hashFloat(hash, value);
        samples += static_cast<unsigned>(count);
    }
    // Regression digest checks double-precision output and state.
    constexpr std::array<std::uint64_t, 4> stereoExpected {0x8f4edc1b8c592484ull,0x2b30ec70855d996full,0xac6b95efd7e5d47eull,0x29478fdc6ffc184cull};
    constexpr std::array<std::uint64_t, 4> routingExpected {0x7d640071c3262278ull,0x9ea3fbe8a3d23e14ull,0xfef01cd0da9e3c7eull,0x14da280b82fcca53ull};
    const auto& expected = routing ? routingExpected : stereoExpected;
    std::printf("ModelDoubleHighRate group %u %s: %llu frames, digest %016llx\n", group, routing ? "routing" : "stereo",
                static_cast<unsigned long long>(samples), static_cast<unsigned long long>(hash));
    return hash == expected[group - 2] ? 0 : 1;
}

int main()
{
    for (unsigned group = 2; group < 6; ++group)
        if (checkGroup(group, false) != 0 || checkGroup(group, true) != 0) return 1;
    return 0;
}
