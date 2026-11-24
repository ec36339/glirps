#include "glirps/constants.hpp"
#include "impl/LfsrNoise.hpp"
#include "impl/Oscillator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <ranges>
#include <vector>

namespace
{
	constexpr std::uint32_t test_seed_lfsr_noise = 0x5a5au;
	constexpr std::uint32_t lfsr_period = 8388607;

	// Renders `sample_count` noise samples at `frequency_hz` by driving one
	// oscillator and one LFSR, counting sample-to-sample value changes in the
	// output buffer. The noise changes value only when the register shifts, so
	// transitions measure the shift rate.
	std::uint32_t countTransitions(std::uint32_t sample_count, float frequency_hz)
	{
		constexpr std::uint32_t sample_rate_hz = 48000;
		// Drive the oscillator at 1x here: the shift-rate relationship under
		// test is factor-independent, and the renderer owns the oversampling.
		glirps::impl::Oscillator oscillator(sample_rate_hz, 1);
		oscillator.setFrequency(frequency_hz);
		glirps::impl::LfsrNoise noise(test_seed_lfsr_noise);

		std::vector<float> buffer(sample_count);
		for (float& sample : buffer)
		{
			if (oscillator.advance())
				noise.shift();
			sample = noise.sample();
		}

		std::uint32_t transitions = 0;
		for (auto&& [previous, current] : std::views::zip(buffer, std::views::drop(buffer, 1)))
		{
			if (current != previous)
				++transitions;
		}
		return transitions;
	}
}

TEST(GlirpsLfsrNoiseTests, Shift_FromSeedUntilRepeat_PeriodIsMaximal)
{
	// ARRANGE
	glirps::impl::LfsrNoise noise(test_seed_lfsr_noise);
	const std::uint32_t start = test_seed_lfsr_noise & 0x7fffffu;

	// ACT
	std::uint32_t state = start;
	std::uint32_t steps = 0;
	while (true)
	{
		state = noise.shift();
		++steps;
		ASSERT_NE(state, 0u);
		if (state == start)
			break;
	}

	// ASSERT
	EXPECT_EQ(steps, lfsr_period);
}

TEST(GlirpsLfsrNoiseTests, Shift_ThroughOscillator_TransitionCountMatchesShiftRate)
{
	// ARRANGE & ACT
	constexpr std::uint32_t sample_count = 48000;
	const std::uint32_t at_440 = countTransitions(sample_count, 440.0f);
	const std::uint32_t at_880 = countTransitions(sample_count, 880.0f);

	// ASSERT
	// 16 shifts per oscillator cycle at 440 Hz over one second is 7040 shifts.
	constexpr float expected_440 = 16.0f * 440.0f;
	EXPECT_NEAR(static_cast<float>(at_440), expected_440, expected_440 * 0.01f);
	constexpr float expected_880 = 16.0f * 880.0f;
	EXPECT_NEAR(static_cast<float>(at_880), expected_880, expected_880 * 0.01f);
	// Doubling the frequency doubles the shift rate.
	EXPECT_NEAR(static_cast<float>(at_880),
		2.0f * static_cast<float>(at_440),
		2.0f * static_cast<float>(at_440) * 0.01f);
}

TEST(GlirpsLfsrNoiseTests, Sample_OverFullPeriod_HasZeroMeanAndReachesBothRails)
{
	// ARRANGE
	glirps::impl::LfsrNoise noise(test_seed_lfsr_noise);

	// ACT
	double sum = 0.0;
	float low = glirps::full_scale;
	float high = -glirps::full_scale;
	for (std::uint32_t i = 0; i < lfsr_period; ++i)
	{
		const float sample = noise.sample();
		sum += static_cast<double>(sample);
		low = std::min(low, sample);
		high = std::max(high, sample);
		noise.shift();
	}
	const double mean = sum / static_cast<double>(lfsr_period);

	// ASSERT
	EXPECT_NEAR(mean, 0.0, 0.01 * glirps::full_scale);
	EXPECT_FLOAT_EQ(low, -glirps::full_scale);
	EXPECT_FLOAT_EQ(high, glirps::full_scale);
}
