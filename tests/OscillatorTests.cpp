#include "impl/LfsrNoise.hpp"
#include "impl/Oscillator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <ranges>
#include <vector>

TEST(GlirpsOscillatorTests, ZeroFrequency_HoldsPhaseAndNoise)
{
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	constexpr std::uint32_t frame_count = 128;
	glirps::impl::Oscillator oscillator(sample_rate_hz);
	oscillator.setFrequency(0.0f);
	glirps::impl::LfsrNoise noise(0x5a5au);

	// ACT
	std::vector<float> noise_samples(frame_count);
	for (float& sample : noise_samples)
	{
		if (oscillator.advance())
			noise.shift();
		sample = noise.sample();
	}

	// ASSERT
	// The phase never advances at zero frequency, so the noise never shifts.
	for (const float sample : std::views::drop(noise_samples, 1))
		EXPECT_EQ(sample, noise_samples.front());

	// The oscillator resumes when the frequency becomes non-zero.
	oscillator.setFrequency(220.0f);
	bool advanced = false;
	for (std::uint32_t i = 0; i < frame_count; ++i)
		advanced = oscillator.advance() || advanced;
	EXPECT_TRUE(advanced);
}
