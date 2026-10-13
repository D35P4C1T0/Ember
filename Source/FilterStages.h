#pragma once

#include <bit>
#include <cstdint>

namespace ember
{
// Finite float inputs and intermediate results are required. Preserve separate
// operations by compiling with floating-point contraction and fast math disabled.
class FilterStages
{
public:
    // Five coefficients per section: feedback1, feedback2, feedforward0,
    // feedforward1, feedforward2. Four states: delay1 L/R, delay2 L/R.
    // Cleanup occurs once per section at the end of each call, including count 0.
    static void cascade(float* left, float* right, int count, const float* coefficients,
                        float* state, int sections) noexcept
    {
        for (int section = 0; section < sections; ++section)
        {
            const float* c = coefficients + section * 5;
            float* s = state + section * 4;
            float previousLeft = s[0], previousRight = s[1];
            float olderLeft = s[2], olderRight = s[3];
            for (int i = 0; i < count; ++i)
            {
                float currentLeft = c[1] * olderLeft;
                currentLeft = currentLeft + left[i];
                currentLeft = currentLeft + c[0] * previousLeft;
                float currentRight = c[1] * olderRight;
                currentRight = currentRight + right[i];
                currentRight = currentRight + c[0] * previousRight;
                float outputLeft = c[3] * previousLeft;
                outputLeft = outputLeft + c[2] * currentLeft;
                outputLeft = outputLeft + c[4] * olderLeft;
                float outputRight = c[3] * previousRight;
                outputRight = outputRight + c[2] * currentRight;
                outputRight = outputRight + c[4] * olderRight;
                left[i] = outputLeft;
                right[i] = outputRight;
                olderLeft = previousLeft;
                olderRight = previousRight;
                previousLeft = currentLeft;
                previousRight = currentRight;
            }
            s[0] = clean(previousLeft);
            s[1] = clean(previousRight);
            s[2] = clean(olderLeft);
            s[3] = clean(olderRight);
        }
    }

    // Coefficients contain five initial values and, when ramp is true, five
    // increments. Increments apply before each sample. Values are local to a call;
    // advancing coefficient storage between calls is the caller's responsibility.
    // State order is accumulator1 L/R, accumulator2 L/R.
    static void accumulating(float* left, float* right, int count,
                             const float* coefficients, float* state, bool ramp) noexcept
    {
        float c0 = coefficients[0], c1 = coefficients[1], c2 = coefficients[2];
        float c3 = coefficients[3], c4 = coefficients[4];
        float firstLeft = state[0], firstRight = state[1];
        float secondLeft = state[2], secondRight = state[3];
        for (int i = 0; i < count; ++i)
        {
            const float inputLeft = left[i], inputRight = right[i];
            if (ramp)
            {
                c0 = c0 + coefficients[5];
                c1 = c1 + coefficients[6];
                c3 = c3 + coefficients[8];
                c2 = c2 + coefficients[7];
                c4 = c4 + coefficients[9];
            }
            float outputLeft = inputLeft * c0;
            outputLeft = outputLeft + firstLeft;
            float outputRight = inputRight * c0;
            outputRight = outputRight + firstRight;
            float nextFirstLeft = firstLeft + secondLeft;
            nextFirstLeft = nextFirstLeft + inputLeft * c1;
            nextFirstLeft = nextFirstLeft + outputLeft * c3;
            float nextFirstRight = firstRight + secondRight;
            nextFirstRight = nextFirstRight + inputRight * c1;
            nextFirstRight = nextFirstRight + outputRight * c3;
            float nextSecondLeft = inputLeft * c2;
            nextSecondLeft = nextSecondLeft + secondLeft;
            nextSecondLeft = nextSecondLeft + outputLeft * c4;
            float nextSecondRight = inputRight * c2;
            nextSecondRight = nextSecondRight + secondRight;
            nextSecondRight = nextSecondRight + outputRight * c4;
            left[i] = outputLeft;
            right[i] = outputRight;
            firstLeft = nextFirstLeft;
            firstRight = nextFirstRight;
            secondLeft = nextSecondLeft;
            secondRight = nextSecondRight;
        }
        state[0] = clean(firstLeft);
        state[1] = clean(firstRight);
        state[2] = clean(secondLeft);
        state[3] = clean(secondRight);
    }

private:
    static float clean(float value) noexcept
    {
        const float magnitude = std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
        return magnitude < 1.0e-30f ? 0.0f : value;
    }
};
}
