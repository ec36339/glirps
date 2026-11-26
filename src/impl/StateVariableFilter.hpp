#pragma once

#include "glirps/FilterMode.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>

namespace glirps::impl
{
	// The state update reflects the integrator outputs back, which is the
	// zero-delay-feedback structure of the topology-preserving filter.
	inline constexpr float integrator_reflection = 2.0f;

	// Topology-preserving (zero-delay-feedback) state variable filter exposing
	// lowpass, bandpass, highpass, and notch outputs simultaneously, matching
	// the reference chip's mode combination behavior. The four responses are
	// selectable and combinable through `FilterModeSet`; an all-false set
	// passes the input through unfiltered rather than emitting silence, so a
	// set that loses its last member is a defined state. A runtime-selectable
	// soft saturation stage sits in the feedback path.
	class StateVariableFilter
	{
	public:
		// Constructs a filter advancing at `sample_rate_hz`.
		explicit StateVariableFilter(std::uint32_t sample_rate_hz) noexcept
			: sample_rate_hz_(sample_rate_hz)
		{
		}

		// Sets cutoff, resonance, mode set, and saturation amount. The
		// prewarped coefficient is derived here; the render path reuses it.
		// Coefficients are recomputed only when cutoff or resonance changes.
		void setParameters(float cutoff_hz, float resonance_q, FilterModeSet modes, float saturation) noexcept
		{
			modes_ = modes;
			saturation_ = saturation;
			if (cutoff_hz == previous_cutoff_hz_ && resonance_q == previous_resonance_q_)
				return;
			previous_cutoff_hz_ = cutoff_hz;
			previous_resonance_q_ = resonance_q;
			const float warped_cutoff
				= std::tan(std::numbers::pi_v<float> * cutoff_hz / static_cast<float>(sample_rate_hz_));
			const float damping = 1.0f / resonance_q;
			coefficient_k_ = damping;
			coefficient_a1_ = 1.0f / (1.0f + warped_cutoff * (warped_cutoff + damping));
			coefficient_a2_ = warped_cutoff * coefficient_a1_;
			coefficient_a3_ = warped_cutoff * coefficient_a2_;
		}

		// Resets for slot reuse.
		void reset() noexcept
		{
			state_ic1_ = 0.0f;
			state_ic2_ = 0.0f;
			modes_ = FilterModeSet{.lowpass = false, .bandpass = false, .highpass = false, .notch = false};
			saturation_ = 0.0f;
			coefficient_k_ = 0.0f;
			coefficient_a1_ = 0.0f;
			coefficient_a2_ = 0.0f;
			coefficient_a3_ = 0.0f;
			// Force the next `setParameters` to recompute even for the same
			// cutoff and resonance the previous instance left behind.
			previous_cutoff_hz_ = -1.0f;
			previous_resonance_q_ = -1.0f;
		}

		// Processes one sample and returns the summed output for the selected
		// modes. An all-false set returns the input unfiltered.
		float process(float input) noexcept
		{
			float low = 0.0f;
			float band = 0.0f;
			float high = 0.0f;
			float notch = 0.0f;

			if (modes_.lowpass || modes_.bandpass || modes_.highpass || modes_.notch)
			{
				// Soft saturation in the feedback path: saturates the signal
				// feeding the integrators when the amount is non-zero.
				float drive = input - state_ic2_;
				if (saturation_ > 0.0f)
				{
					const float saturated = std::tanh(drive * (1.0f + saturation_));
					drive = drive + saturation_ * (saturated - drive);
				}

				const float bandpass = coefficient_a1_ * state_ic1_ + coefficient_a2_ * drive;
				const float lowpass = state_ic2_ + coefficient_a2_ * state_ic1_ + coefficient_a3_ * drive;

				state_ic1_ = integrator_reflection * bandpass - state_ic1_;
				state_ic2_ = integrator_reflection * lowpass - state_ic2_;

				low = lowpass;
				band = bandpass;
				high = input - coefficient_k_ * bandpass - lowpass;
				notch = low + high;
			}

			float output = 0.0f;
			if (modes_.lowpass)
				output += low;
			if (modes_.bandpass)
				output += band;
			if (modes_.highpass)
				output += high;
			if (modes_.notch)
				output += notch;

			const bool any_response = modes_.lowpass || modes_.bandpass || modes_.highpass || modes_.notch;
			return any_response ? output : input;
		}

	private:
		std::uint32_t sample_rate_hz_;
		FilterModeSet modes_;
		float saturation_ = 0.0f;
		float coefficient_k_ = 0.0f;
		float coefficient_a1_ = 0.0f;
		float coefficient_a2_ = 0.0f;
		float coefficient_a3_ = 0.0f;
		float state_ic1_ = 0.0f;
		float state_ic2_ = 0.0f;
		// The cutoff and resonance `setParameters` last derived coefficients
		// for, so it can skip the derivation when neither moved. Initialized
		// to -1 so the first call always recomputes (cutoff and resonance are
		// never negative).
		float previous_cutoff_hz_ = -1.0f;
		float previous_resonance_q_ = -1.0f;
	};
}
