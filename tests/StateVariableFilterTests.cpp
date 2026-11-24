#include "glirps/constants.hpp"
#include "impl/StateVariableFilter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace
{
	constexpr std::uint32_t test_sample_rate_hz_variable_filter = 48000;

	// Feeds a sine at `frequency_hz` through the filter, discarding the first
	// 4096 samples as transient, and returns the RMS of the steady-state output
	// in dB relative to the input RMS.
	double magnitudeDb(const glirps::impl::StateVariableFilter& filter,
		float frequency_hz,
		std::uint32_t discard_samples = 4096)
	{
		// Compute an integer number of periods after the transient.
		const float period_samples = static_cast<float>(test_sample_rate_hz_variable_filter) / frequency_hz;
		const std::uint32_t periods = 10;
		const std::uint32_t measure_samples
			= static_cast<std::uint32_t>(std::lround(period_samples * static_cast<float>(periods)));

		std::vector<float> input(measure_samples);
		std::uint32_t next_sample_index = 0;
		for (float& sample : input)
		{
			const std::uint32_t i = next_sample_index++;
			const float t
				= static_cast<float>(discard_samples + i) / static_cast<float>(test_sample_rate_hz_variable_filter);
			sample = std::sin(2.0f * std::numbers::pi_v<float> * frequency_hz * t);
		}

		glirps::impl::StateVariableFilter run = filter;
		for (std::uint32_t i = 0; i < discard_samples; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(test_sample_rate_hz_variable_filter);
			run.process(std::sin(2.0f * std::numbers::pi_v<float> * frequency_hz * t));
		}

		double sum_input = 0.0;
		double sum_output = 0.0;
		for (const float sample : input)
		{
			const float out = run.process(sample);
			sum_input += static_cast<double>(sample) * static_cast<double>(sample);
			sum_output += static_cast<double>(out) * static_cast<double>(out);
		}
		const double rms_input = std::sqrt(sum_input / static_cast<double>(measure_samples));
		const double rms_output = std::sqrt(sum_output / static_cast<double>(measure_samples));
		return glirps::decibel_amplitude_scale * std::log10(rms_output / rms_input);
	}
}

TEST(StateVariableFilterTests, Lowpass_AtCutoff_IsThreeDbDown)
{
	// ARRANGE
	constexpr float cutoff_hz = 1000.0f;
	constexpr float resonance_q = 0.707f;
	glirps::impl::StateVariableFilter filter(test_sample_rate_hz_variable_filter);
	glirps::FilterModeSet modes;
	modes.lowpass = true;
	filter.setParameters(cutoff_hz, resonance_q, modes, 0.0f);

	// ACT
	const double magnitude = magnitudeDb(filter, cutoff_hz);

	// ASSERT
	EXPECT_NEAR(magnitude, -3.0, 1.0);
}

TEST(StateVariableFilterTests, Lowpass_TwoOctavesAboveCutoff_IsTwentyFourDbDown)
{
	// ARRANGE
	constexpr float cutoff_hz = 1000.0f;
	constexpr float resonance_q = 0.707f;
	glirps::impl::StateVariableFilter filter(test_sample_rate_hz_variable_filter);
	glirps::FilterModeSet modes;
	modes.lowpass = true;
	filter.setParameters(cutoff_hz, resonance_q, modes, 0.0f);

	// ACT
	const double magnitude = magnitudeDb(filter, cutoff_hz * 4.0f);

	// ASSERT
	EXPECT_NEAR(magnitude, -24.0, 3.0);
}

TEST(StateVariableFilterTests, Modes_LowpassAlone_IsThreeDbDownAtCutoff)
{
	// ARRANGE
	constexpr float cutoff_hz = 1000.0f;
	constexpr float resonance_q = 0.707f;
	glirps::impl::StateVariableFilter filter(test_sample_rate_hz_variable_filter);
	glirps::FilterModeSet lp;
	lp.lowpass = true;
	filter.setParameters(cutoff_hz, resonance_q, lp, 0.0f);

	// ACT & ASSERT
	EXPECT_NEAR(magnitudeDb(filter, cutoff_hz), -3.0, 1.0);
}

TEST(StateVariableFilterTests, Modes_BandpassAlone_IsThreeDbDownAtCutoff)
{
	// ARRANGE
	constexpr float cutoff_hz = 1000.0f;
	constexpr float resonance_q = 0.707f;
	glirps::impl::StateVariableFilter filter(test_sample_rate_hz_variable_filter);
	glirps::FilterModeSet bp;
	bp.lowpass = false;
	bp.bandpass = true;
	filter.setParameters(cutoff_hz, resonance_q, bp, 0.0f);

	// ACT & ASSERT: bandpass gain at cutoff equals Q; with Q=0.707 that is
	// -3 dB.
	EXPECT_NEAR(magnitudeDb(filter, cutoff_hz), -3.0, 1.0);
}

TEST(StateVariableFilterTests, Modes_HighpassAlone_IsThreeDbDownAtCutoff)
{
	// ARRANGE
	constexpr float cutoff_hz = 1000.0f;
	constexpr float resonance_q = 0.707f;
	glirps::impl::StateVariableFilter filter(test_sample_rate_hz_variable_filter);
	glirps::FilterModeSet hp;
	hp.lowpass = false;
	hp.highpass = true;
	filter.setParameters(cutoff_hz, resonance_q, hp, 0.0f);

	// ACT & ASSERT
	EXPECT_NEAR(magnitudeDb(filter, cutoff_hz), -3.0, 1.0);
}

TEST(StateVariableFilterTests, Modes_NotchAlone_NullsAtCutoff)
{
	// ARRANGE
	constexpr float cutoff_hz = 1000.0f;
	constexpr float resonance_q = 0.707f;
	glirps::impl::StateVariableFilter filter(test_sample_rate_hz_variable_filter);
	glirps::FilterModeSet notch;
	notch.lowpass = false;
	notch.notch = true;
	filter.setParameters(cutoff_hz, resonance_q, notch, 0.0f);

	// ACT & ASSERT: notch nulls at cutoff; assert a deep null rather than a
	// numeric gain.
	EXPECT_LT(magnitudeDb(filter, cutoff_hz), -30.0);
}

TEST(StateVariableFilterTests, Modes_LowpassAndHighpassCombined_SumToUnityAcrossPassband)
{
	// ARRANGE
	constexpr float cutoff_hz = 1000.0f;
	constexpr float resonance_q = 0.707f;
	glirps::impl::StateVariableFilter filter(test_sample_rate_hz_variable_filter);
	glirps::FilterModeSet sum;
	sum.lowpass = true;
	sum.highpass = true;
	filter.setParameters(cutoff_hz, resonance_q, sum, 0.0f);

	// ACT & ASSERT
	EXPECT_NEAR(magnitudeDb(filter, cutoff_hz * 0.25f), 0.0, 1.0);
}

TEST(StateVariableFilterTests, Modes_AllFalse_PassesInputUnfiltered)
{
	// ARRANGE
	constexpr float cutoff_hz = 1000.0f;
	constexpr float resonance_q = 0.707f;
	glirps::impl::StateVariableFilter filter(test_sample_rate_hz_variable_filter);
	glirps::FilterModeSet none;
	none.lowpass = false;
	filter.setParameters(cutoff_hz, resonance_q, none, 0.0f);

	// ACT & ASSERT
	for (const float frequency : {cutoff_hz * 0.25f, cutoff_hz, cutoff_hz * 4.0f})
		EXPECT_NEAR(magnitudeDb(filter, frequency), 0.0, 0.1);
}
