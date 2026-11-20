#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace xt
{
/// Detects transient markers in mono source frames. Run on a background thread.
std::vector<int64_t> detectOnsets (std::span<const float> mono, double sampleRate, double sensitivity);
} // namespace xt
