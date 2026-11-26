#pragma once

#include "glirps/GritProfile.hpp"
#include "glirps/ParameterBlock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace glirps::impl
{
	namespace detail
	{
		// Gain steps are sized over `[gain_floor, gain_authored_ceiling]`;
		// higher gains stay on the same lattice.
		using glirps::frequency_hz_floor;
		using glirps::gain_authored_ceiling;
		using glirps::gain_floor;
		using glirps::pulse_width_ceiling;
		using glirps::pulse_width_floor;
		using glirps::resonance_q_ceiling;
		using glirps::resonance_q_floor;

		// MOS 6581 (SID) reference constants, so the reference grit profile's
		// bit depths quantize against the range the real chip actually spans
		// at that bit depth, rather than the field's full digital range.
		//
		// SID frequency formula: `Fout = Fn * Fclk / 2^24`. The pitch
		// reference range is `Fout` at the largest `Fn` and the PAL clock, so
		// at `reference_pitch_bits` and full grit one step equals one register
		// LSB.
		constexpr float sid_pal_clock_hz = 985248.0f;
		constexpr std::uint32_t sid_phase_accumulator_bits = 24;
		constexpr std::uint32_t sid_frequency_register_bits = 16;
		constexpr float sid_frequency_register_max
			= static_cast<float>((std::uint64_t{1} << sid_frequency_register_bits) - 1);
		constexpr float sid_pitch_reference_range_hz = sid_pal_clock_hz * sid_frequency_register_max
			/ static_cast<float>(std::uint64_t{1} << sid_phase_accumulator_bits);

		// The 11-bit cutoff register drives a nonlinear analog filter core
		// whose span, approximated by the floor and ceiling below, covers
		// only part of the audible band; the nonlinearity itself stays
		// unmodeled.
		constexpr float sid_cutoff_reference_floor_hz = 30.0f;
		constexpr float sid_cutoff_reference_ceiling_hz = 12000.0f;

		// Real per-voice level comes from the 8-bit ADSR/DAC envelope stage,
		// not from a bit-depth-authored register: `bit_depths.gain` matches
		// only the chip-wide 4-bit master volume register, applied to
		// `output_gain` below. Per-voice `gain` uses this fixed reference
		// instead.
		constexpr std::uint32_t sid_envelope_dac_bits = 8;

		// The quantization step size at full bit depth: `range` split into
		// `2^bits - 1` levels. `grit_scale` then interpolates the step actually
		// applied between 0, full float resolution, and this value at 1.
		// Interpolating the step rather than the integer bit depth makes the
		// scale sweep continuously instead of jumping at each integer bit.
		[[nodiscard]] inline float quantizationStep(std::uint32_t bits, float range, float grit_scale) noexcept
		{
			const float levels = static_cast<float>((std::uint64_t{1} << bits) - 1);
			const float full_step = range / levels;
			return grit_scale * full_step;
		}

		// Snaps `value` onto the lattice `min` plus a multiple of `step`, per
		// `mode`. A non-positive `step` is the scale-zero identity transform,
		// so a grit scale of 0 leaves every field unchanged.
		[[nodiscard]] inline float quantize(float value, float min, float step, SteppingMode mode) noexcept
		{
			if (step <= 0.0f)
				return value;
			const float units = (value - min) / step;
			const float stepped = mode == SteppingMode::round ? std::round(units) : std::floor(units);
			return min + stepped * step;
		}
	}

	// Quantizes every numeric field of `block` to the grit lattice `profile`
	// and `grit_scale` describe. All lo-fi character is carried here, as a
	// quantization pass on the evaluated block before it reaches the chip
	// instance. `grit_scale` interpolates the quantization **step size** of
	// each parameter between full float resolution at 0, the identity
	// transform, and `profile`'s bit depths at 1.
	// Pitch and cutoff quantize against a fixed SID 6581 reference range
	// rather than the field's full digital range, so this needs no sample
	// rate. Does not clamp; the caller does.
	inline void applyGrit(ParameterBlock& block, const GritProfile& profile, float grit_scale) noexcept
	{
		const SteppingMode mode = profile.stepping_mode;

		const float cutoff_step = detail::quantizationStep(profile.bit_depths.cutoff,
			detail::sid_cutoff_reference_ceiling_hz - detail::sid_cutoff_reference_floor_hz,
			grit_scale);
		block.cutoff_hz = detail::quantize(block.cutoff_hz, detail::sid_cutoff_reference_floor_hz, cutoff_step, mode);

		const float resonance_step = detail::quantizationStep(profile.bit_depths.resonance,
			detail::resonance_q_ceiling - detail::resonance_q_floor,
			grit_scale);
		block.resonance_q = detail::quantize(block.resonance_q, detail::resonance_q_floor, resonance_step, mode);

		const float gain_step = detail::quantizationStep(profile.bit_depths.gain,
			detail::gain_authored_ceiling - detail::gain_floor,
			grit_scale);
		block.output_gain = detail::quantize(block.output_gain, detail::gain_floor, gain_step, mode);

		const float pitch_step
			= detail::quantizationStep(profile.bit_depths.pitch, detail::sid_pitch_reference_range_hz, grit_scale);
		const float pulse_width_step = detail::quantizationStep(profile.bit_depths.pulse_width,
			detail::pulse_width_ceiling - detail::pulse_width_floor,
			grit_scale);
		// Per-voice level is the 8-bit envelope/DAC stage, not
		// `bit_depths.gain`'s 4-bit master volume register; see the
		// constant's declaration above.
		const float voice_gain_step = detail::quantizationStep(detail::sid_envelope_dac_bits,
			detail::gain_authored_ceiling - detail::gain_floor,
			grit_scale);

		for (VoiceParameters& voice : std::span<VoiceParameters>(block.voices.data(), block.voice_count))
		{
			voice.frequency_hz = detail::quantize(voice.frequency_hz, detail::frequency_hz_floor, pitch_step, mode);
			voice.pulse_width = detail::quantize(voice.pulse_width, detail::pulse_width_floor, pulse_width_step, mode);
			voice.gain = detail::quantize(voice.gain, detail::gain_floor, voice_gain_step, mode);
		}
	}
}
