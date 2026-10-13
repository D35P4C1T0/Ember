#include "../Source/BlockScheduler.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace
{
void require(bool condition)
{
    if (!condition)
        std::abort();
}

struct Event
{
    std::int32_t kind;
    std::int32_t value;
    std::int32_t offset;
    std::int32_t phase;
    bool operator==(const Event&) const = default;
};

std::vector<Event> process(ember::BlockScheduler& scheduler, int count)
{
    std::vector<Event> events;
    int processed = 0;
    scheduler.process(count,
        [&](int offset, int span)
        {
            require(span > 0);
            require(offset == processed);
            events.push_back({ 0, span, offset, scheduler.phase() });
            processed += span;
        },
        [&](bool forced)
        {
            events.push_back({ 1, forced ? 1 : 0, processed, scheduler.phase() });
        });
    require(processed == (count > 0 ? count : 0));
    return events;
}

// Optional binary fixtures provide expected scheduler callback sequences.
void compareFixtures(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    auto read = [&](void* pointer, std::size_t size)
    {
        input.read(static_cast<char*>(pointer), static_cast<std::streamsize>(size));
        require(static_cast<bool>(input));
    };
    std::uint32_t magic = 0, cases = 0;
    read(&magic, sizeof(magic));
    read(&cases, sizeof(cases));
    require(magic == 0x45425331 && cases > 0 && cases <= 1000000);
    for (std::uint32_t index = 0; index < cases; ++index)
    {
        std::array<std::int32_t, 6> header;
        read(header.data(), sizeof(header));
        const auto [period, initialPhase, count, forced, expectedPhase, eventCount] = header;
        require(period > 0 && initialPhase >= 0 && initialPhase <= period);
        require(count >= 0 && count <= 65536 && (forced == 0 || forced == 1));
        require(expectedPhase >= 0 && expectedPhase <= period);
        require(eventCount >= 0 && eventCount <= 131074);
        std::vector<Event> expected(static_cast<std::size_t>(eventCount));
        read(expected.data(), expected.size() * sizeof(Event));
        ember::BlockScheduler scheduler(period, initialPhase);
        if (forced)
            scheduler.requestUpdate();
        require(process(scheduler, count) == expected);
        require(scheduler.phase() == expectedPhase && !scheduler.updatePending());
    }
    require(input.peek() == std::char_traits<char>::eof());
}
}

int main(int argc, char** argv)
{
    using ember::BlockScheduler;
    for (int period : { 256, 512, 1024 })
    {
        BlockScheduler clock(period);
        require(process(clock, period - 1) == std::vector<Event> { { 0, period - 1, 0, 0 } });
        require(process(clock, 1) == std::vector<Event> { { 0, 1, 0, period - 1 } });
        require(clock.phase() == period);
        require(process(clock, 0).empty());
        require(clock.phase() == period);
        require(process(clock, 1) == std::vector<Event> { { 1, 0, 0, period }, { 0, 1, 0, 0 } });
        require(process(clock, period * 2) == std::vector<Event> {
            { 0, period - 1, 0, 1 }, { 1, 0, period - 1, period },
            { 0, period, period - 1, 0 }, { 1, 0, period * 2 - 1, period },
            { 0, 1, period * 2 - 1, 0 } });
        clock.reset(18);
        clock.requestUpdate();
        require(process(clock, period + 144) == std::vector<Event> {
            { 1, 1, 0, period }, { 0, 1, 0, 0 }, { 0, period - 1, 1, 1 },
            { 1, 0, period, period }, { 0, 144, period, 0 } });
        require(clock.phase() == 144);

        // Forced changes on an empty block consume the flag, leaving a normal
        // control boundary pending for the next positive-length block.
        clock.requestUpdate();
        require(process(clock, 0).empty());
        require(clock.phase() == period && !clock.updatePending());
        require(process(clock, 1) == std::vector<Event> { { 1, 0, 0, period }, { 0, 1, 0, 0 } });
        clock.reset();
        require(clock.phase() == 0 && !clock.updatePending());
        require(process(clock, -1).empty());
    }
    if (argc == 2)
        compareFixtures(argv[1]);
    else
        require(argc == 1);
}
