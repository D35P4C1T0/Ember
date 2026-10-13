#pragma once

#include <algorithm>

namespace ember
{
// Audio spans and control updates follow a persistent sample clock. The caller
// supplies a positive period and an initial phase in [0, period].
class BlockScheduler
{
public:
    explicit BlockScheduler(int periodSamples = 256, int initialPhase = 0) noexcept
        : periodValue(periodSamples), phaseValue(initialPhase)
    {
    }

    int period() const noexcept { return periodValue; }
    int phase() const noexcept { return phaseValue; }
    bool updatePending() const noexcept { return pending; }

    // A pending update is consumed at entry to process, including an empty block.
    void requestUpdate() noexcept { pending = true; }

    void reset(int initialPhase = 0) noexcept
    {
        phaseValue = initialPhase;
        pending = false;
    }

    // Audio receives the offset within this host block and a positive count.
    // Control receives true for a forced update, false for a clock boundary.
    // The phase remains at period during the control callback and resets after it.
    template <class Audio, class Control>
    void process(int count, Audio&& audio, Control&& control)
    {
        bool forced = pending;
        pending = false;
        if (forced)
            phaseValue = periodValue;
        int offset = 0;
        while (offset < count)
        {
            int span = std::min(periodValue - phaseValue, count - offset);
            if (forced)
                span = 1;
            if (phaseValue == periodValue)
            {
                control(forced);
                forced = false;
                phaseValue = 0;
            }
            if (span > 0)
            {
                audio(offset, span);
                phaseValue += span;
                offset += span;
            }
        }
    }

private:
    int periodValue;
    int phaseValue;
    bool pending = false;
};
}
