#pragma once

#include "glirps/constants.hpp"

#include <cmath>
#include <cstdint>

namespace glirps::impl
{
	// One oscillator cycle in phase units, matching the 6581's phase
	// accumulator width.
	inline constexpr std::uint32_t oscillator_phase_span = 1u << 24;

	// The accumulator bit whose rising edge clocks the noise register, as on
	// the 6581.
	inline constexpr std::uint32_t noise_clock_bit_mask = 1u << 19;

	// Initial pulse width (square wave).
	inline constexpr float default_pulse_width = 0.5f;

	// Waveform formulas over the normalized phase t in [0, 1).
	inline constexpr float triangle_slope = 4.0f;
	inline constexpr float triangle_center = 0.5f;
	inline constexpr float sawtooth_slope = 2.0f;

	// Phase-accumulator oscillator producing triangle, sawtooth, and
	// variable-width pulse. Advancing the phase is separate from reading the
	// waveform, so the caller drives the phase at the generation rate and reads
	// the selected waveform. `advance`'s return value reports the noise clock
	// edge, keeping the oscillator and the noise register decoupled types
	// that a voice combines.
	class Oscillator
	{
	public:
		Oscillator() noexcept = default;

		// Generates at `sample_rate_hz * oversampling_factor`, so the caller
		// advances `oversampling_factor` times per output sample and the
		// decimator brings the result back to the output rate.
		explicit Oscillator(std::uint32_t sample_rate_hz,
			std::uint32_t oversampling_factor = default_oversampling_factor) noexcept
			: generation_rate_hz_(sample_rate_hz * oversampling_factor)
		{
		}

		// Sets the oscillator frequency. A frequency of zero holds the phase,
		// so `advance` never reports a clock edge and the noise register is not
		// clocked.
		void setFrequency(float frequency_hz) noexcept
		{
			if (frequency_hz <= 0.0f)
			{
				phase_increment_ = 0;
				return;
			}
			phase_increment_ = static_cast<std::uint32_t>(std::lround(
				frequency_hz / static_cast<float>(generation_rate_hz_) * static_cast<float>(oscillator_phase_span)));
		}

		// Sets the pulse width (duty cycle) used by the pulse waveform.
		void setPulseWidth(float duty) noexcept { pulse_width_ = duty; }

		// Advances the phase by one sample and returns whether accumulator
		// bit 19 rose from 0 to 1, which is the noise register's clock edge.
		// With a held phase this returns false, since no edge occurred.
		[[nodiscard]] bool advance() noexcept
		{
			const std::uint32_t previous = phase_;
			const std::uint32_t next = phase_ + phase_increment_;
			overflowed_ = next >= oscillator_phase_span;
			phase_ = next & (oscillator_phase_span - 1);
			return (previous & noise_clock_bit_mask) == 0 && (phase_ & noise_clock_bit_mask) != 0;
		}

		// Returns whether the most recent `advance` wrapped the accumulator,
		// which is the overflow another voice's hard sync is driven by.
		[[nodiscard]] bool didOverflow() const noexcept { return overflowed_; }

		// Resets the phase to zero, which is what hard sync does to a voice
		// whose sync source overflowed.
		void resetPhase() noexcept { phase_ = 0; }

		// Resets for slot reuse.
		void reset() noexcept
		{
			phase_ = 0;
			phase_increment_ = 0;
			overflowed_ = false;
			pulse_width_ = default_pulse_width;
		}

		// Bipolar triangle, -1.0 at phase 0 rising to +1.0 at mid-cycle.
		[[nodiscard]] float triangle() const noexcept
		{
			const float t = static_cast<float>(phase_) / static_cast<float>(oscillator_phase_span);
			return 1.0f - triangle_slope * std::fabs(t - triangle_center);
		}

		// Bipolar rising sawtooth, -1.0 at phase 0 approaching +1.0 at wrap.
		[[nodiscard]] float sawtooth() const noexcept
		{
			const float t = static_cast<float>(phase_) / static_cast<float>(oscillator_phase_span);
			return sawtooth_slope * t - 1.0f;
		}

		// Bipolar variable-width pulse, +1.0 for the high fraction of the cycle.
		[[nodiscard]] float pulse() const noexcept
		{
			const float t = static_cast<float>(phase_) / static_cast<float>(oscillator_phase_span);
			return t < pulse_width_ ? 1.0f : -1.0f;
		}

	private:
		std::uint32_t generation_rate_hz_ = 0;
		std::uint32_t phase_ = 0;
		std::uint32_t phase_increment_ = 0;
		bool overflowed_ = false;
		float pulse_width_ = default_pulse_width;
	};
}
