#pragma once

#include "Allocator.hpp"
#include "BesselI0.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

namespace glirps::impl
{
	// Linear-phase decimation FIR designed with a Kaiser window. The band
	// edges are relative to the output rate: the stopband begins at the
	// output Nyquist, attenuated by at least `stopband_db`, and the passband
	// ends one transition width below it, flat within 0.1 dB. At a 48000 Hz
	// output this gives a passband to 20 kHz and a stopband from 24 kHz.
	// Taps are computed once at construction and never on the render path.
	// Only the decimated output samples are computed, which avoids filtering
	// samples that would be discarded.
	class Decimator
	{
	public:
		// Constructs a decimation FIR for `output_rate_hz` at
		// `oversampling_factor`, computing its taps once at construction. The
		// tap and history buffers allocate through `allocator`, so a failed
		// allocation throws `std::bad_alloc`. Both buffers are sized in their
		// constructors rather than built from the allocator alone: the
		// allocator-only `std::vector` constructor is `noexcept`, yet a debug
		// standard library may allocate bookkeeping in it, so a failed
		// allocation there would terminate instead of throwing.
		Decimator(std::uint32_t output_rate_hz,
			std::uint32_t oversampling_factor,
			const AllocatorRef& allocator = AllocatorRef())
			: output_rate_hz_(output_rate_hz)
			, oversampling_factor_(oversampling_factor)
			, generation_rate_hz_(output_rate_hz * oversampling_factor)
			, taps_(computeTapCount(output_rate_hz_, generation_rate_hz_), 0.0f, Allocator<float>(allocator))
			, history_(taps_.size(), 0.0f, Allocator<float>(allocator))
		{
			computeTaps();
		}

		// Recomputes the taps and clears history for a new `output_rate_hz` and
		// `oversampling_factor`.
		void reconfigure(std::uint32_t output_rate_hz, std::uint32_t oversampling_factor) noexcept
		{
			output_rate_hz_ = output_rate_hz;
			oversampling_factor_ = oversampling_factor;
			generation_rate_hz_ = output_rate_hz * oversampling_factor;
			computeTaps();
			history_.assign(taps_.size(), 0.0f);
			write_pos_ = 0;
			phase_ = 0;
		}

		// Feeds one oversampled input sample. Returns the decimated output
		// every `oversampling_factor` inputs, otherwise no value.
		std::optional<float> process(float input) noexcept
		{
			history_.at(write_pos_) = input;
			write_pos_ = (write_pos_ + 1) % history_.size();

			++phase_;
			if (phase_ < oversampling_factor_)
				return std::nullopt;
			phase_ = 0;

			float sum = 0.0f;
			std::size_t index = (write_pos_ + history_.size() - 1) % history_.size();
			for (const float tap : taps_)
			{
				sum += tap * history_.at(index);
				index = (index + history_.size() - 1) % history_.size();
			}
			return sum;
		}

		// Group delay in decimated output frames, `(taps - 1) / (2 * factor)`.
		[[nodiscard]] std::uint32_t getGroupDelayInFrames() const noexcept
		{
			return static_cast<std::uint32_t>((taps_.size() - 1) / (2 * oversampling_factor_));
		}

		// Returns the decimator to its construction state: zero history, write
		// position and phase at their starts. Used when a chip slot is handed
		// to a new instance so the previous occupant's FIR history cannot
		// colour the new sound's onset.
		void reset() noexcept
		{
			history_.assign(taps_.size(), 0.0f);
			write_pos_ = 0;
			phase_ = 0;
		}

	private:
		// Kaiser design with band edges relative to the output rate so no
		// rate aliases: the stopband begins at the output Nyquist, which
		// is where folding starts, and the transition band is one sixth
		// of it, giving 20 kHz / 24 kHz band edges exactly at 48000 Hz. The transition width in radians is then
		// rate-invariant, so the tap count depends only on the
		// oversampling factor. The tap count and shape parameter are
		// derived from the band edges and attenuation rather than chosen
		// by hand.
		static constexpr double stopband_db = 80.0;

		[[nodiscard]] static double getStopbandHz(std::uint32_t output_rate_hz) noexcept
		{
			return 0.5 * static_cast<double>(output_rate_hz);
		}

		[[nodiscard]] static double getTransitionHz(double stopband_hz) noexcept { return stopband_hz / 6.0; }

		// Returns the odd Kaiser tap count for `output_rate_hz` generated at
		// `generation_rate_hz`.
		[[nodiscard]] static std::uint32_t computeTapCount(std::uint32_t output_rate_hz,
			std::uint32_t generation_rate_hz) noexcept
		{
			// Kaiser filter order estimate constants.
			constexpr double kaiser_numerator_offset = 7.95;
			constexpr double kaiser_denominator_factor = 2.285;

			const double transition_hz = getTransitionHz(getStopbandHz(output_rate_hz));
			const double transition_rad
				= 2.0 * std::numbers::pi * transition_hz / static_cast<double>(generation_rate_hz);
			std::uint32_t taps = static_cast<std::uint32_t>(
				std::ceil((stopband_db - kaiser_numerator_offset) / (kaiser_denominator_factor * transition_rad)));
			if (taps % 2 == 0)
				++taps;  // odd, symmetric center tap
			return taps;
		}

		void computeTaps()
		{
			const double stopband_hz = getStopbandHz(output_rate_hz_);
			const double passband_hz = stopband_hz - getTransitionHz(stopband_hz);
			const double beta = 0.1102 * (stopband_db - 8.7);
			const std::uint32_t taps = computeTapCount(output_rate_hz_, generation_rate_hz_);

			const double cutoff_hz = 0.5 * (passband_hz + stopband_hz);
			const double fc = cutoff_hz / static_cast<double>(generation_rate_hz_);
			const double mid = 0.5 * static_cast<double>(taps - 1);
			const double inv_beta = 1.0 / besselI0(beta);

			taps_.assign(taps, 0.0f);
			double gain = 0.0;
			for (std::uint32_t n = 0; n < taps; ++n)
			{
				const double x = static_cast<double>(n) - mid;
				// Ideal sinc lowpass impulse response.
				const double ideal
					= x == 0.0 ? 2.0 * fc : std::sin(2.0 * std::numbers::pi * fc * x) / (std::numbers::pi * x);
				// Kaiser window.
				const double normalized = (2.0 * x) / static_cast<double>(taps - 1);
				const double arg = beta * std::sqrt(std::max(0.0, 1.0 - normalized * normalized));
				const double window = besselI0(arg) * inv_beta;
				const double tap = ideal * window;
				taps_.at(n) = static_cast<float>(tap);
				gain += tap;
			}
			// Normalise to unity DC gain, so the passband sits at 0 dB.
			for (float& tap : taps_)
				tap = static_cast<float>(static_cast<double>(tap) / gain);
		}

		std::uint32_t output_rate_hz_;
		std::uint32_t oversampling_factor_;
		std::uint32_t generation_rate_hz_;
		std::vector<float, Allocator<float>> taps_;
		std::vector<float, Allocator<float>> history_;
		std::uint32_t write_pos_ = 0;
		std::uint32_t phase_ = 0;
	};
}
