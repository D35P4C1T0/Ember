#include "../Source/FloatEngine.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>

namespace
{
struct Random
{
    std::uint32_t state = 310002u;
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
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (int i = 0; i < 4; ++i)
    {
        hash ^= (bits >> (i * 8)) & 255u;
        hash *= 1099511628211ull;
    }
}
}

int main()
{
    for (double rate : {22050.0,32000.0,384000.0})
    {
        ember::FloatEngine fallback,standard;
        if (!fallback.prepare(rate) || fallback.rateGroup()!=0 || !standard.prepare(44100.0)) return 1;
        std::array<float,513> input,leftA,rightA,leftB,rightB;
        for (unsigned i=0;i<input.size();++i) input[i]=static_cast<float>(int(i%17)-8)*0.0625f;
        fallback.process(input.data(),input.data(),leftA.data(),rightA.data(),int(input.size()),0.73f,0.5f,0.0f,false,false);
        standard.process(input.data(),input.data(),leftB.data(),rightB.data(),int(input.size()),0.73f,0.5f,0.0f,false,false);
        if (leftA!=leftB || rightA!=rightB) return 1;
    }
    ember::FloatEngine fractional;
    if (!fractional.prepare(48000.9) || fractional.rateGroup()!=1) return 1;
    if (fractional.prepare(0.0) || fractional.isReady()) return 1;
    constexpr std::array<unsigned, 6> rates {44100,48000,88200,96000,176400,192000};
    constexpr std::array<int, 5> counts {0,1,64,257,1024};
    Random random;
    std::array<float, 1024> left{},right{},outputLeft{},outputRight{},inPlaceLeft{},inPlaceRight{};
    std::uint64_t hash = 14695981039346656037ull;
    std::uint64_t samples = 0;
    for (unsigned rate : rates)
    {
        ember::FloatEngine engine;
        if (!engine.prepare(rate)) return 1;
        float amount = 0.0f,mode = 0.5f,bypass = 0.0f;
        // State and the control clock persist across host calls, including empty
        // calls and changes whose first audio span crosses a clock boundary.
        for (int trial = 0; trial < 2000; ++trial)
        {
            const int count = counts[static_cast<unsigned>(trial) % counts.size()];
            if (trial % 31 == 0)
            {
                amount = random.unit();
                mode = static_cast<float>(random.next() % 3) * 0.5f;
                bypass = trial % 63 > 30 ? 1.0f : 0.0f;
            }
            const bool inputMono = (trial & 1) != 0;
            const bool outputMono = (trial & 2) != 0;
            for (int i = 0; i < count; ++i)
            {
                left[i] = (random.unit() - 0.5f) * 0.5f;
                right[i] = (random.unit() - 0.5f) * 0.5f;
            }
            if (trial % 7 == 0)
            {
                inPlaceLeft = left;
                inPlaceRight = right;
                engine.process(inPlaceLeft.data(),inPlaceRight.data(),inPlaceLeft.data(),inPlaceRight.data(),
                               count,amount,mode,bypass,inputMono,outputMono);
                outputLeft = inPlaceLeft;
                outputRight = inPlaceRight;
            }
            else
                engine.process(left.data(),right.data(),outputLeft.data(),outputRight.data(),
                               count,amount,mode,bypass,inputMono,outputMono);
            for (int i = 0; i < count; ++i)
            {
                hashFloat(hash,outputLeft[i]);
                if (!outputMono) hashFloat(hash,outputRight[i]);
            }
            samples += static_cast<unsigned>(count);
        }
    }
    // Regression digest covers scheduling, routing, and audio output.
    constexpr std::uint64_t expected = 0x24a6711a4a663462ull;
    std::printf("FloatEngine: %llu scheduled frames, digest %016llx\n",
                static_cast<unsigned long long>(samples),static_cast<unsigned long long>(hash));
    return hash == expected ? 0 : 1;
}
