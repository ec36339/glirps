#pragma once

/// @file
/// Quantization and stepping parameters of the grit stage.

#include <cstdint>

namespace glirps
{
	/// How a value is mapped onto its quantization step.
	enum class SteppingMode
	{
		/// Drops the low bits. Reference-hardware behavior.
		truncate,
		/// Snaps to the nearest step rather than the one below.
		round,
	};

	/// Default quantization resolutions, in bits: the MOS 6581 register widths
	/// the reference grit profile models. This one is the pitch resolution.
	inline constexpr std::uint32_t reference_pitch_bits = 16;
	/// Default pulse width resolution, in bits.
	inline constexpr std::uint32_t reference_pulse_width_bits = 12;
	/// Default filter cutoff resolution, in bits.
	inline constexpr std::uint32_t reference_cutoff_bits = 11;
	/// Default filter resonance resolution, in bits.
	inline constexpr std::uint32_t reference_resonance_bits = 4;
	/// Default output gain resolution, in bits.
	inline constexpr std::uint32_t reference_gain_bits = 4;

	/// Quantization resolution in bits, one entry per parameter class.
	struct BitDepths
	{
		/// Resolution of a voice's frequency.
		std::uint32_t pitch = reference_pitch_bits;
		/// Resolution of a voice's pulse width.
		std::uint32_t pulse_width = reference_pulse_width_bits;
		/// Resolution of the shared filter's cutoff.
		std::uint32_t cutoff = reference_cutoff_bits;
		/// Resolution of the shared filter's resonance.
		std::uint32_t resonance = reference_resonance_bits;
		/// Resolution of the chip-wide output gain.
		std::uint32_t gain = reference_gain_bits;
	};

	/// The quantization and stepping parameter set applied at each effective
	/// tick. `tick_multiplier` sets the result of `synthGetBlocksPerBaseTick`;
	/// `stepping_mode` and `bit_depths` drive the quantization of every
	/// evaluated parameter.
	struct GritProfile
	{
		/// Effective ticks per base tick.
		std::uint32_t tick_multiplier = 1;
		/// How each quantized value is mapped onto its step.
		SteppingMode stepping_mode = SteppingMode::truncate;
		/// The quantization resolution of each parameter class.
		BitDepths bit_depths;
	};
}
