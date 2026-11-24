#pragma once

#include "glirps/glirps_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

// Shared helpers for the tests of the free functions in `glirps/glirps_render.hpp`.
namespace glirps::test_support
{
	inline constexpr std::uint32_t test_sample_rate_hz = 48000;
	inline constexpr std::uint32_t test_oversampling_factor = 16;
	inline constexpr float test_frequency_hz = 220.0f;
	inline constexpr std::uint64_t test_seed = 0x2A5A5A5Au;

	inline float peakOf(std::span<const float> samples) noexcept
	{
		float peak = 0.0f;
		for (const float sample : samples)
			peak = std::max(peak, std::fabs(sample));
		return peak;
	}
}
