#include "../Source/Model44.h"
#include "../Source/ModelTables.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>

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

int main(int argc, char**)
{
    using ember::Model44;
    const auto* data = ember::floatModelTables(0);
    if (data == nullptr) return 1;
    Model44::State state;
    std::copy((*data)[1].begin(), (*data)[1].end(), state.audio.begin());
    std::copy((*data)[3].begin(), (*data)[3].end(), state.tracking.begin());
    std::copy((*data)[5].begin(), (*data)[5].end(), state.derived.begin());
    std::copy((*data)[4].begin(), (*data)[4].end(), state.meters.begin());
    std::copy((*data)[7].begin(), (*data)[7].end(), state.filters.begin());
    const Model44::Tables tables { (*data)[0].data(), (*data)[2].data(), (*data)[6].data() };
    const bool routing = argc > 1;
    Random random;
    std::array<float, 256> left, right, outputLeft, outputRight;
    outputRight.fill(0.0f);
    std::uint64_t hash = 14695981039346656037ull;
    std::uint64_t samples = 0;
    // Deterministic automation exercises endpoints, all block sizes, bypass,
    // coefficient interpolation and a long silence that reaches the idle gate.
    for (int trial = 0; trial < 12000; ++trial)
    {
        const int count = 1 + static_cast<int>(random.next() % 256);
        const bool immediate = trial == 0 || (trial < 1000 && trial % 271 == 0);
        const bool blockSlew = trial < 1000 && trial % 3 == 1;
        const float amount = trial < 1000 ? (trial % 7 == 0 ? 0.0f : trial % 7 == 1 ? 1.0f : random.unit()) : 0.5f;
        const float mode = trial < 1000 ? static_cast<float>(random.next() % 3) * 0.5f : 0.0f;
        const float bypass = trial < 1000 && trial % 137 > 109 ? 1.0f : 0.0f;
        const float outputGain = 0.5f + random.unit();
        Model44::Controls controls { amount, mode, 44100.0f, bypass, 0.0f };
        Model44::update(state, tables, controls, count, immediate, blockSlew);
        hashFloat(hash, controls.auxiliary);
        hashFloat(hash, controls.envelope);
        for (int i = 0; i < count; ++i)
        {
            left[i] = trial > 1000 || trial % 199 > 150 ? 0.0f : (random.unit() - 0.5f) * 0.8f;
            right[i] = trial > 1000 || trial % 199 > 150 ? 0.0f : (random.unit() - 0.5f) * 0.8f;
        }
        if (routing)
            Model44::processChannels(state, tables, left.data(), right.data(), outputLeft.data(), outputRight.data(), count, outputGain, (trial & 1) != 0, (trial & 2) != 0);
        else
            Model44::processStereo(state, tables, left.data(), right.data(), outputLeft.data(), outputRight.data(), count, outputGain);
        for (int i = 0; i < count; ++i)
        {
            hashFloat(hash, outputLeft[i]);
            hashFloat(hash, outputRight[i]);
        }
        for (float value : state.audio) hashFloat(hash, value);
        for (float value : state.tracking) hashFloat(hash, value);
        for (float value : state.derived) hashFloat(hash, value);
        for (float value : state.meters) hashFloat(hash, value);
        for (float value : state.filters) hashFloat(hash, value);
        samples += static_cast<unsigned>(count);
    }
    // Regression digest checks deterministic output and state across architectures.
    const std::uint64_t expected = routing ? 0x6d940c8d82bb592dull : 0x0953f3bbdfb25b84ull;
    std::printf("Model44: %llu stereo pairs, digest %016llx\n",
                static_cast<unsigned long long>(samples), static_cast<unsigned long long>(hash));
    return hash == expected ? 0 : 1;
}
