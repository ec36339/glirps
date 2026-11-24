#include "glirps/constants.hpp"
#include "impl/BlockRenderer.hpp"
#include "impl/Decimator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace
{
	constexpr std::uint32_t output_rate_hz = 48000;
	constexpr std::uint32_t oversampling_factor = 4;

	// Bound on the RMS difference between the 16x and 64x renders of the
	// swept sawtooth, in dBFS. A decimation regression shifts the figure by
	// many dB, far beyond the tolerance.
	constexpr float convergence_golden_dbfs = -53.71f;
	// Absorbs cross-platform float differences in std::pow/std::log10.
	constexpr float convergence_tolerance_db = 0.1f;
	// The factor under test, compared against the reference factor below.
	constexpr std::uint32_t convergence_factor_under_test = 16;
	constexpr std::uint32_t convergence_reference_factor = 64;
	constexpr std::uint32_t convergence_frame_count = 48000 * 2;
	constexpr float convergence_start_frequency_hz = 100.0f;
	constexpr float convergence_end_frequency_hz = 8000.0f;
	constexpr std::uint64_t convergence_seed = 0x2A5A5A5Au;
	constexpr float convergence_unused_pulse_width = 0.5f;

	// Measures the decimator's gain in dB for a pure tone at `frequency_hz`,
	// fed at the oversampled rate for one second of output, by the same
	// RMS-over-steady-state method as the two 48000 Hz tests below: the
	// FIR's turn-on transient is discarded before measuring, and the tone is
	// generated in double precision so the input carries no broadband noise
	// floor of its own.
	double measureToneGainDb(std::uint32_t rate_hz, float frequency_hz)
	{
		glirps::impl::Decimator decimator(rate_hz, oversampling_factor);
		const std::uint32_t input_samples = rate_hz * oversampling_factor;
		constexpr std::uint32_t transient_output_samples = 1000;
		std::vector<float> output;
		output.reserve(input_samples / oversampling_factor);
		double sum_input = 0.0;
		double sum_output = 0.0;
		for (std::uint32_t i = 0; i < input_samples; ++i)
		{
			const double t = static_cast<double>(i) / static_cast<double>(rate_hz * oversampling_factor);
			const float input
				= static_cast<float>(std::sin(2.0 * std::numbers::pi * static_cast<double>(frequency_hz) * t));
			sum_input += static_cast<double>(input) * static_cast<double>(input);
			if (const auto sample = decimator.process(input); sample.has_value())
				output.push_back(*sample);
		}
		for (const float sample :
			std::span<const float>(output).subspan(std::min<std::size_t>(transient_output_samples, output.size())))
		{
			const double value = static_cast<double>(sample);
			sum_output += value * value;
		}
		const double rms_input = std::sqrt(sum_input / static_cast<double>(input_samples));
		const double steady_output_count = static_cast<double>(output.size() - transient_output_samples);
		const double rms_output = std::sqrt(sum_output / steady_output_count);
		return glirps::decibel_amplitude_scale * std::log10(rms_output / rms_input);
	}

	// Renders the convergence sweep: a
	// single sawtooth voice swept exponentially from 100 Hz to 8000 Hz over two
	// seconds through the oversampled path, returning the decimated output and
	// the decimator's group delay in frames.
	void renderConvergenceSweep(std::vector<float>& output, std::uint32_t& group_delay, std::uint32_t factor)
	{
		output.resize(convergence_frame_count);
		glirps::impl::BlockRenderer renderer(output_rate_hz, factor);
		renderer.configureVoice(0,
			glirps::Waveform::sawtooth,
			convergence_start_frequency_hz,
			convergence_unused_pulse_width,
			1.0f,
			convergence_seed);
		renderer.setGate(0, true);
		group_delay = renderer.getDecimatorGroupDelayInFrames();
		for (std::uint32_t frame = 0; frame < convergence_frame_count; ++frame)
		{
			const float t = static_cast<float>(frame) / static_cast<float>(convergence_frame_count - 1);
			const float frequency = convergence_start_frequency_hz
				* std::pow(convergence_end_frequency_hz / convergence_start_frequency_hz, t);
			renderer.setVoiceFrequency(0, frequency);
			output.at(frame) = renderer.renderOne();
		}
	}
}

TEST(DecimatorTests, AboveOutputNyquist_AttenuatedByStopbandFigure)
{
	// ARRANGE
	// A sine above the output Nyquist (24 kHz) must be in the stopband.
	constexpr float input_frequency_hz = 30000.0f;
	glirps::impl::Decimator decimator(output_rate_hz, oversampling_factor);

	// ACT
	// Feed the oversampled sine for one second of output (192000 input
	// samples), collecting the decimated output. Discard the FIR's turn-on
	// transient before measuring, as the filter starts from zero initial
	// conditions; the filter's tap count is well under the discarded span.
	// The sine is generated in double precision: generating it in float
	// accumulates a broadband noise floor in the input itself that the
	// passband passes, which would swamp the stopband rejection under test.
	constexpr std::uint32_t input_samples = output_rate_hz * oversampling_factor;
	constexpr std::uint32_t transient_output_samples = 1000;
	std::vector<float> output;
	output.reserve(input_samples / oversampling_factor);
	double sum_input = 0.0;
	double sum_output = 0.0;
	for (std::uint32_t i = 0; i < input_samples; ++i)
	{
		const double t = static_cast<double>(i) / static_cast<double>(output_rate_hz * oversampling_factor);
		const float input = static_cast<float>(std::sin(2.0 * std::numbers::pi * input_frequency_hz * t));
		sum_input += static_cast<double>(input) * static_cast<double>(input);
		if (const auto sample = decimator.process(input); sample.has_value())
			output.push_back(*sample);
	}
	for (const float sample :
		std::span<const float>(output).subspan(std::min<std::size_t>(transient_output_samples, output.size())))
	{
		const double value = static_cast<double>(sample);
		sum_output += value * value;
	}
	const double rms_input = std::sqrt(sum_input / static_cast<double>(input_samples));
	const double steady_output_count = static_cast<double>(output.size() - transient_output_samples);
	const double rms_output = std::sqrt(sum_output / steady_output_count);

	// ASSERT
	// The stopband attenuates by at least 80 dB from 24 kHz.
	const double attenuation = glirps::decibel_amplitude_scale * std::log10(rms_output / rms_input);
	EXPECT_LE(attenuation, -80.0);
}

TEST(DecimatorTests, Passband_Below20kHz_IsTransparent)
{
	// ARRANGE
	// The passband extends to 20 kHz within 0.1 dB; a tone well
	// below it must pass essentially unchanged.
	constexpr float input_frequency_hz = 8000.0f;
	glirps::impl::Decimator decimator(output_rate_hz, oversampling_factor);

	// ACT
	constexpr std::uint32_t input_samples = output_rate_hz * oversampling_factor;
	constexpr std::uint32_t transient_output_samples = 1000;
	std::vector<float> output;
	output.reserve(input_samples / oversampling_factor);
	double sum_input = 0.0;
	double sum_output = 0.0;
	for (std::uint32_t i = 0; i < input_samples; ++i)
	{
		const double t = static_cast<double>(i) / static_cast<double>(output_rate_hz * oversampling_factor);
		const float input = static_cast<float>(std::sin(2.0 * std::numbers::pi * input_frequency_hz * t));
		sum_input += static_cast<double>(input) * static_cast<double>(input);
		if (const auto sample = decimator.process(input); sample.has_value())
			output.push_back(*sample);
	}
	for (const float sample :
		std::span<const float>(output).subspan(std::min<std::size_t>(transient_output_samples, output.size())))
	{
		const double value = static_cast<double>(sample);
		sum_output += value * value;
	}
	const double rms_input = std::sqrt(sum_input / static_cast<double>(input_samples));
	const double steady_output_count = static_cast<double>(output.size() - transient_output_samples);
	const double rms_output = std::sqrt(sum_output / steady_output_count);

	// ASSERT
	const double gain = glirps::decibel_amplitude_scale * std::log10(rms_output / rms_input);
	EXPECT_NEAR(gain, 0.0, 0.1);
}

TEST(DecimatorTests, AboveOutputNyquist_At22050_AttenuatedByStopbandFigure)
{
	// ARRANGE
	// The band edges track the output rate: at a
	// 22050 Hz output the stopband begins at the 11025 Hz Nyquist, so a
	// 15 kHz tone must be attenuated by the 80 dB stopband figure.
	constexpr std::uint32_t rate_hz = 22050;
	constexpr float input_frequency_hz = 15000.0f;

	// ACT
	const double attenuation = measureToneGainDb(rate_hz, input_frequency_hz);

	// ASSERT
	EXPECT_LE(attenuation, -80.0);
}

TEST(DecimatorTests, Passband_At22050_IsTransparent)
{
	// ARRANGE
	// At a 22050 Hz output the passband ends at five sixths of the 11025 Hz
	// Nyquist, about 9188 Hz, so an 8 kHz tone must pass within the 0.1 dB
	// passband ripple.
	constexpr std::uint32_t rate_hz = 22050;
	constexpr float input_frequency_hz = 8000.0f;

	// ACT
	const double gain = measureToneGainDb(rate_hz, input_frequency_hz);

	// ASSERT
	EXPECT_NEAR(gain, 0.0, 0.1);
}

TEST(DecimatorTests, SweptSawtooth_16xVersus64x_RmsWithinGoldenBound)
{
	// ARRANGE
	// Render the same sweep at both factors.
	std::vector<float> at_factor;
	std::vector<float> at_64x;
	std::uint32_t delay_factor = 0;
	std::uint32_t delay_64x = 0;
	renderConvergenceSweep(at_factor, delay_factor, convergence_factor_under_test);
	renderConvergenceSweep(at_64x, delay_64x, convergence_reference_factor);

	// ACT
	// Align on each filter's group delay, then differ the overlap.
	const std::uint32_t start = std::max(delay_factor, delay_64x);
	double sum_squares = 0.0;
	std::uint32_t count = 0;
	for (std::uint32_t i = start; i < convergence_frame_count; ++i)
	{
		const double diff = static_cast<double>(at_factor.at(i)) - static_cast<double>(at_64x.at(i));
		sum_squares += diff * diff;
		++count;
	}
	const float rms = static_cast<float>(std::sqrt(sum_squares / static_cast<double>(count)));
	const float rms_dbfs = glirps::decibel_amplitude_scale * std::log10(rms / glirps::full_scale);

	// ASSERT
	EXPECT_LE(rms_dbfs, convergence_golden_dbfs + convergence_tolerance_db);
}
