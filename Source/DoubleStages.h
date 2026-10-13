#pragma once
#include <array>
#include <bit>
#include <cstdint>

namespace ember
{
// Finite double arithmetic. Disable contraction and fast math.
class DoubleControlStages
{
public:
    static void coefficientRamp(int count, int prefixCount, const double* input,
                                double* prefix, double* state, double slew,
                                double residualGain, double* output, double mix) noexcept
    {
        const double complement = 1.0 - mix;
        for (int i = 0; i < prefixCount; ++i)
            prefix[i] = complement * prefix[i];
        for (int i = 0; i < count; ++i)
        {
            double value = complement * state[i];
            double contribution = input[i] * mix;
            value = value + contribution;
            output[i] = value;
            double end = value - input[i];
            end = end * slew;
            end = end + input[i];
            state[i] = end;
            end = end - value;
            output[count + i] = end * residualGain;
        }
    }

    // The curve uses 23 intervals. Both curve and coefficient tables must
    // include the next element/row even when an interpolation fraction is zero.
    static void interpolateRows(const double* curve, int rows, const double* table,
                                int width, double* output, double mix) noexcept
    {
        double position = mix * 23.0;
        int curveIndex = static_cast<int>(position);
        if (position < 0.0)
            --curveIndex;
        double fraction = position - static_cast<double>(curveIndex);
        double mapped = curve[curveIndex + 1] - curve[curveIndex];
        mapped = mapped * fraction;
        mapped = mapped + curve[curveIndex];
        position = mapped * static_cast<double>(rows - 1);
        int row = static_cast<int>(position);
        if (position < 0.0)
            --row;
        fraction = position - static_cast<double>(row);
        for (int i = 0; i < width; ++i)
        {
            const int index = row * width + i;
            double value = table[index + width] - table[index];
            value = value * fraction;
            output[i] = value + table[index];
        }
    }

    static void targetRamp(double& current, double* output, int count, double target,
                           double step, bool immediate) noexcept
    {
        if (immediate)
        {
            for (int i = 0; i < count; ++i)
                output[i] = target;
            current = target;
            return;
        }
        if (count <= 0)
            return;
        const bool descending = current >= target;
        if (descending)
            step = std::bit_cast<double>(std::bit_cast<std::uint64_t>(step) ^ 0x8000000000000000ull);
        const double first = current + step;
        std::array<double, 2> lanes;
        for (int lane = 0; lane < 2; ++lane)
        {
            double value = static_cast<double>(lane) * step;
            lanes[static_cast<std::size_t>(lane)] = value + first;
        }
        const double groupStep = 2.0 * step;
        const int groupedCount = count & ~1;
        int i = 0;
        for (; i < groupedCount; i += 2)
        {
            for (int lane = 0; lane < 2; ++lane)
            {
                const double value = lanes[static_cast<std::size_t>(lane)];
                lanes[static_cast<std::size_t>(lane)] = value + groupStep;
                output[i + lane] = descending ? (value > target ? value : target)
                                             : (value < target ? value : target);
            }
        }
        for (; i < count; ++i)
        {
            double value = static_cast<double>(i) * step;
            value = value + first;
            output[i] = descending ? (value > target ? value : target)
                                   : (value < target ? value : target);
        }
        current = output[count - 1];
    }
};

class DoubleFilterStages
{
public:
    // Five coefficients per section: feedback1, feedback2, feedforward0,
    // feedforward1, feedforward2. Four states: delay1 L/R, delay2 L/R.
    // Cleanup occurs once per section at the end of each call, including count 0.
    static void cascade(double* left, double* right, int count, const double* coefficients,
                        double* state, int sections) noexcept
    {
        for (int section = 0; section < sections; ++section)
        {
            const double* c = coefficients + section * 5;
            double* s = state + section * 4;
            double previousLeft = s[0], previousRight = s[1];
            double olderLeft = s[2], olderRight = s[3];
            for (int i = 0; i < count; ++i)
            {
                double currentLeft = c[1] * olderLeft;
                currentLeft = currentLeft + left[i];
                currentLeft = currentLeft + c[0] * previousLeft;
                double currentRight = c[1] * olderRight;
                currentRight = currentRight + right[i];
                currentRight = currentRight + c[0] * previousRight;
                double outputLeft = c[3] * previousLeft;
                outputLeft = outputLeft + c[2] * currentLeft;
                outputLeft = outputLeft + c[4] * olderLeft;
                double outputRight = c[3] * previousRight;
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
    static void accumulating(double* left, double* right, int count,
                             const double* coefficients, double* state, bool ramp) noexcept
    {
        double c0 = coefficients[0], c1 = coefficients[1], c2 = coefficients[2];
        double c3 = coefficients[3], c4 = coefficients[4];
        double firstLeft = state[0], firstRight = state[1];
        double secondLeft = state[2], secondRight = state[3];
        for (int i = 0; i < count; ++i)
        {
            const double inputLeft = left[i], inputRight = right[i];
            if (ramp)
            {
                c0 = c0 + coefficients[5];
                c1 = c1 + coefficients[6];
                c3 = c3 + coefficients[8];
                c2 = c2 + coefficients[7];
                c4 = c4 + coefficients[9];
            }
            double outputLeft = inputLeft * c0;
            outputLeft = outputLeft + firstLeft;
            double outputRight = inputRight * c0;
            outputRight = outputRight + firstRight;
            double nextFirstLeft = firstLeft + secondLeft;
            nextFirstLeft = nextFirstLeft + inputLeft * c1;
            nextFirstLeft = nextFirstLeft + outputLeft * c3;
            double nextFirstRight = firstRight + secondRight;
            nextFirstRight = nextFirstRight + inputRight * c1;
            nextFirstRight = nextFirstRight + outputRight * c3;
            double nextSecondLeft = inputLeft * c2;
            nextSecondLeft = nextSecondLeft + secondLeft;
            nextSecondLeft = nextSecondLeft + outputLeft * c4;
            double nextSecondRight = inputRight * c2;
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
    static double clean(double value) noexcept
    {
        const double magnitude = std::bit_cast<double>(std::bit_cast<std::uint64_t>(value) & 0x7fffffffffffffffull);
        return magnitude < 1.0e-300 ? 0.0 : value;
    }
};

class DoubleLookupStage
{
public:
    struct Coordinate
    {
        int row;
        double fraction;
    };

    static Coordinate amplitudeCoordinate(double amplitude) noexcept
    {
        if (amplitude == 0.0)
            return { 23, 0.0 };

        const auto bits = std::bit_cast<std::uint64_t>(amplitude);
        const int exponent = static_cast<int>((bits >> 52) & 2047u) - 1022;
        int row = 1 - exponent;
        double fraction = std::bit_cast<double>((bits & 0xbfefffffffffffffull) | 0x3fe0000000000000ull);
        if (row > 22)
        {
            auto mantissaBits = std::bit_cast<std::uint64_t>(fraction);
            auto shifted = (static_cast<std::uint64_t>(23 - row) << 52)
                         + (mantissaBits & 0xfff0000000000000ull);
            shifted = ((shifted ^ mantissaBits) & 0x7ff0000000000000ull) ^ mantissaBits;
            fraction = std::bit_cast<double>(shifted);
            row = 23;
        }
        else
        {
            fraction = fraction - 0.5;
            fraction = fraction * 2.0;
        }
        return { row, fraction };
    }

    // Separate double operations are intentional; compile with contraction disabled.
    static double interpolate(const double* centeredTable, int index,
                             double signalFraction, double amplitudeFraction) noexcept
    {
        double next = centeredTable[index + 66] - centeredTable[index + 65];
        next = next * signalFraction;
        double current = centeredTable[index + 1] - centeredTable[index];
        next = next + centeredTable[index + 65];
        current = current * signalFraction;
        current = current + centeredTable[index];
        current = current - next;
        current = current * amplitudeFraction;
        return current + next;
    }

    static void process(double* samples, int count, double release, double& envelope,
                        const double* centeredTable, double* scratch, int* indices,
                        double envelopeGain, int multiplier) noexcept
    {
        if (count <= 0)
            return;
        for (int i = 0; i < count; ++i)
            scratch[i] = absolute(samples[i]);

        double previous = envelope;
        for (int i = 0; i < count; ++i)
        {
            double decayed = previous - scratch[i];
            decayed = decayed * release;
            decayed = scratch[i] + decayed;
            const double scaled = samples[i] * 0x1.6a09e6p-1;
            const double magnitude = absolute(scaled);
            previous = magnitude > decayed ? magnitude : decayed;
            previous = previous > 1.0e-300 ? previous : 1.0e-300;
            samples[i] = scaled;
            scratch[i] = previous;
        }
        envelope = previous;

        const double multiplierFloat = static_cast<double>(multiplier);
        for (int i = 0; i < count; ++i)
        {
            samples[i] = samples[i] / scratch[i];
            double amplitude = scratch[i] * envelopeGain;
            amplitude = amplitude * multiplierFloat;
            amplitude = amplitude > -0x1.fffffc00146c0p-1 ? amplitude : -0x1.fffffc00146c0p-1;
            amplitude = amplitude < 0x1.fffffc00146c0p-1 ? amplitude : 0x1.fffffc00146c0p-1;
            scratch[i] = amplitude;
        }
        for (int i = 0; i < count; ++i)
        {
            const auto coordinate = amplitudeCoordinate(scratch[i]);
            const double position = 31.0 * samples[i];
            int column = static_cast<int>(position);
            if (position < 0.0)
                --column;
            indices[i] = coordinate.row * 65 + column;
            scratch[i] = coordinate.fraction;
            samples[i] = position - static_cast<double>(column);
        }
        for (int i = 0; i < count; ++i)
            samples[i] = interpolate(centeredTable, indices[i], samples[i], scratch[i]);
    }

private:
    static double absolute(double value) noexcept
    {
        return std::bit_cast<double>(std::bit_cast<std::uint64_t>(value) & 0x7fffffffffffffffull);
    }
};
}
