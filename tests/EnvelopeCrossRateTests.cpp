#include "impl/Envelope.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
	// Advances the envelope until `predicate` is true and returns the number
	// of samples taken.
	template<typename Predicate>
	std::uint32_t advanceUntilCrossRate(glirps::impl::Envelope& envelope, Predicate predicate)
	{
		std::uint32_t samples = 0;
		while (!predicate(envelope.advance()))
			++samples;
		return samples;
	}

	// Measures attack, decay, and release duration in seconds for an
	// envelope constructed at `sample_rate_hz`, at fixed crossing levels:
	// attack at 0.99, decay within 1 percent of sustain, release below 0.001.
	struct MeasuredDurations
	{
		float attack_seconds = 0.0f;
		float decay_seconds = 0.0f;
		float release_seconds = 0.0f;
	};

	MeasuredDurations measure(std::uint32_t sample_rate_hz,
		float attack_seconds,
		float decay_seconds,
		float release_seconds,
		float sustain_level)
	{
		glirps::impl::Envelope envelope(sample_rate_hz);
		envelope.setRatesInSeconds(attack_seconds, decay_seconds, release_seconds, sustain_level);
		envelope.setGate(true);

		MeasuredDurations measured;
		const std::uint32_t attack_samples
			= advanceUntilCrossRate(envelope, [](float level) { return level >= 0.99f; });
		measured.attack_seconds = static_cast<float>(attack_samples) / static_cast<float>(sample_rate_hz);

		const std::uint32_t decay_samples
			= advanceUntilCrossRate(envelope, [sustain_level](float level) { return level <= sustain_level * 1.01f; });
		measured.decay_seconds = static_cast<float>(decay_samples) / static_cast<float>(sample_rate_hz);

		envelope.setGate(false);
		const std::uint32_t release_samples
			= advanceUntilCrossRate(envelope, [](float level) { return level < 0.001f; });
		measured.release_seconds = static_cast<float>(release_samples) / static_cast<float>(sample_rate_hz);

		return measured;
	}
}

TEST(EnvelopeCrossRateTests, SetRatesInSeconds_AtDifferentSampleRates_ProduceSameDurationWithinOnePercent)
{
	// ARRANGE: envelope rates are converted from seconds to samples at
	// `synthStart`, using whatever rate `configure` fixed, never on the render
	// path. A device granting 44100 Hz rather than the offline path's fixed
	// 48000 Hz SHALL still produce the authored duration in real time.
	constexpr float attack_seconds = 0.0183f;
	constexpr float decay_seconds = 0.501f;
	constexpr float release_seconds = 1.512f;
	constexpr float sustain_level = 0.5f;

	// ACT
	const MeasuredDurations at_48000 = measure(48000, attack_seconds, decay_seconds, release_seconds, sustain_level);
	const MeasuredDurations at_44100 = measure(44100, attack_seconds, decay_seconds, release_seconds, sustain_level);

	// ASSERT
	EXPECT_NEAR(at_44100.attack_seconds, at_48000.attack_seconds, at_48000.attack_seconds * 0.01f);
	EXPECT_NEAR(at_44100.decay_seconds, at_48000.decay_seconds, at_48000.decay_seconds * 0.01f);
	EXPECT_NEAR(at_44100.release_seconds, at_48000.release_seconds, at_48000.release_seconds * 0.01f);
}
