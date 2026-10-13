#pragma once
#include <array>
#include <span>

namespace ember
{
using FloatModelTables = std::array<std::span<const float>, 8>;
using DoubleModelTables = std::array<std::span<const double>, 8>;
// Coefficient tables use the group ordering below.
// Group index order is 44100, 48000, 88200, 96000, 176400, 192000 Hz.
const FloatModelTables* floatModelTables(unsigned group) noexcept;
const DoubleModelTables* doubleModelTables(unsigned group) noexcept;
}
