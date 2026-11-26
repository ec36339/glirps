#pragma once

#include "impl/ChipInstance.hpp"

#include <cstdint>

namespace glirps::impl
{
	// Test-only access to `ChipInstance`'s smoothed ramp state.
	struct ChipInstanceTestAccess
	{
		// The chip's currently smoothed filter cutoff, in hertz.
		[[nodiscard]] static float cutoffHz(const ChipInstance& chip) noexcept { return chip.cutoff_current_; }

		// The chip's currently smoothed filter resonance, as a Q factor.
		[[nodiscard]] static float resonanceQ(const ChipInstance& chip) noexcept { return chip.resonance_current_; }

		// The chip's currently smoothed output gain, as a linear multiplier.
		[[nodiscard]] static float outputGain(const ChipInstance& chip) noexcept { return chip.output_gain_current_; }

		// One voice's currently smoothed gain.
		[[nodiscard]] static float voiceGain(const ChipInstance& chip, std::uint32_t voice_index) noexcept
		{
			return chip.voices_.at(voice_index).gain_current;
		}
	};
}
