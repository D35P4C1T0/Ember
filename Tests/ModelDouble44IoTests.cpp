#include "../Source/ModelDouble44.h"
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
template <typename T>
void hashFloat(std::uint64_t& hash, T value)
{
    const auto bytes = std::bit_cast<std::array<unsigned char, sizeof(T)>>(value);
    for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }

}
}

int main(int argc, char** argv)
{
    using ember::ModelDouble44;
    const auto* data = ember::doubleModelTables(argc > 2 ? 1u : 0u);
    if (data == nullptr) return 1;
    ModelDouble44::State state;
    std::copy((*data)[1].begin(), (*data)[1].end(), state.audio.begin());
    std::copy((*data)[3].begin(), (*data)[3].end(), state.tracking.begin());
    std::copy((*data)[5].begin(), (*data)[5].end(), state.derived.begin());
    std::copy((*data)[4].begin(), (*data)[4].end(), state.meters.begin());
    std::copy((*data)[7].begin(), (*data)[7].end(), state.filters.begin());
    const ModelDouble44::Tables tables { (*data)[0].data(), (*data)[2].data(), (*data)[6].data() };
    const bool routing = argc > 1 && argv[1][0] != 's';
    const bool rate48 = argc > 2;
    const auto& tuning = rate48 ? ModelDouble44::tuning48 : ModelDouble44::defaultTuning;
    Random random;
    std::array<double, 256> left, right, outputLeft, outputRight;
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
        ModelDouble44::Controls controls { amount, mode, (rate48 ? 48000.0 : 44100.0), bypass, 0.0f };
        ModelDouble44::update(state, tables, controls, count, immediate, blockSlew, tuning);
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
            ModelDouble44::processChannelsDouble(state, tables, left.data(), right.data(), outputLeft.data(), outputRight.data(), count, outputGain, (trial & 1) != 0, (trial & 2) != 0, tuning);
        else
            ModelDouble44::processStereoDouble(state, tables, left.data(), right.data(), outputLeft.data(), outputRight.data(), count, outputGain, tuning);
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
    const std::uint64_t expected = rate48 ? (routing ? 0x0fe4c58d8b5acaa1ull : 0xa2aa833070aa42d5ull)
                                          : (routing ? 0x80943b32fa122e06ull : 0xec7bcf804b638cabull);
    std::printf("ModelDouble44: %llu stereo pairs, digest %016llx\n",
                static_cast<unsigned long long>(samples), static_cast<unsigned long long>(hash));
    return hash == expected ? 0 : 1;
}
