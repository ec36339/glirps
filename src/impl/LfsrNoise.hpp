#pragma once

#include "glirps/constants.hpp"

#include <cstdint>

namespace glirps::impl
{
	// 23-bit linear feedback shift register noise generator reproducing the
	// MOS 6581 noise source. The register shifts on the rising edge of bit 19
	// of the oscillator's phase accumulator, so the noise is pitched rather
	// than white. The feedback polynomial and output tap list are verified
	// against reSID.
	class LfsrNoise
	{
	public:
		LfsrNoise() noexcept
			: register_(1)
		{
		}

		// Constructs with a non-zero seed. All zero is an absorbing state and
		// is unreachable from a non-zero seed.
		explicit LfsrNoise(std::uint32_t seed) noexcept
			: register_(seed & register_mask)
		{
			if (register_ == 0)
				register_ = 1;
		}

		// Shifts the register one step and returns the new register value.
		std::uint32_t shift() noexcept
		{
			const std::uint32_t feedback = ((register_ >> 22) ^ (register_ >> 17)) & 1u;
			register_ = ((register_ << 1) | feedback) & register_mask;
			return register_;
		}

		// The current output sample, assembled from the verified reSID taps
		// (register bits 20, 18, 14, 11, 9, 5, 2, 0) into an 8-bit code and
		// mapped as `(code - 127.5) / 127.5` scaled by `full_scale`.
		[[nodiscard]] float sample() const noexcept
		{
			const std::uint32_t code = ((register_ >> 20) & 1u) << 7 | ((register_ >> 18) & 1u) << 6
				| ((register_ >> 14) & 1u) << 5 | ((register_ >> 11) & 1u) << 4 | ((register_ >> 9) & 1u) << 3
				| ((register_ >> 5) & 1u) << 2 | ((register_ >> 2) & 1u) << 1 | ((register_ >> 0) & 1u);
			return (static_cast<float>(code) - code_center) / code_center * full_scale;
		}

	private:
		static constexpr std::uint32_t register_mask = 0x7fffffu;
		// Center of the 8-bit code domain; `(code - 127.5) / 127.5` maps the
		// range 0..255 onto exactly -1.0..+1.0 with zero mean.
		static constexpr float code_center = 127.5f;
		std::uint32_t register_;
	};
}
