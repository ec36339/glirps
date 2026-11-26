#include "impl/Envelope.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
	constexpr std::uint32_t test_sample_rate_hz_envelope = 48000;

	// Advances the envelope until `predicate` is true and returns the number
	// of samples taken.
	template<typename Predicate>
	std::uint32_t advanceUntilEnvelope(glirps::impl::Envelope& envelope, Predicate predicate)
	{
		std::uint32_t samples = 0;
		while (!predicate(envelope.advance()))
			++samples;
		return samples;
	}
}

TEST(EnvelopeTests, Durations_MatchRateTableWithinOnePercent)
{
	// ARRANGE
	// attack index 4 (0.0183 s), decay index 8 (0.501 s), release index 10
	// (1.512 s), sustain 0.5.
	constexpr float attack_seconds = 0.0183f;
	constexpr float decay_seconds = 0.501f;
	constexpr float release_seconds = 1.512f;
	constexpr float sustain_level = 0.5f;

	glirps::impl::Envelope envelope(test_sample_rate_hz_envelope);
	ASSERT_TRUE(envelope.setRates(4, 8, 10, sustain_level));

	// ACT
	envelope.setGate(true);

	// Attack: first sample reaching 0.99.
	const std::uint32_t attack_samples = advanceUntilEnvelope(envelope, [](float level) { return level >= 0.99f; });
	const float measured_attack = static_cast<float>(attack_samples) / static_cast<float>(test_sample_rate_hz_envelope);

	// Decay: the first sample within 1 percent of the sustain level.
	// `advanceUntilEnvelope` counts from the envelope's current state on each
	// call, so no prior call's count is subtracted.
	const std::uint32_t decay_samples
		= advanceUntilEnvelope(envelope, [sustain_level](float level) { return level <= sustain_level * 1.01f; });
	const float measured_decay = static_cast<float>(decay_samples) / static_cast<float>(test_sample_rate_hz_envelope);

	// Release: gate off at the sustain level, then until below 0.001.
	envelope.setGate(false);
	const std::uint32_t release_samples = advanceUntilEnvelope(envelope, [](float level) { return level < 0.001f; });
	const float measured_release
		= static_cast<float>(release_samples) / static_cast<float>(test_sample_rate_hz_envelope);

	// ASSERT
	EXPECT_NEAR(measured_attack, attack_seconds, attack_seconds * 0.01f);
	EXPECT_NEAR(measured_decay, decay_seconds, decay_seconds * 0.01f);
	EXPECT_NEAR(measured_release, release_seconds, release_seconds * 0.01f);
}

// A sustain of zero decays to silence over the authored decay duration rather
// than collapsing to one sample.
TEST(EnvelopeTests, ZeroSustainDecaysToSilenceOverDecayDuration)
{
	// ARRANGE
	constexpr float decay_seconds = 0.18f;
	glirps::impl::Envelope envelope(test_sample_rate_hz_envelope);
	envelope.setRatesInSeconds(0.002f, decay_seconds, 0.06f, 0.0f);

	// ACT
	envelope.setGate(true);
	// Attack to full level, then measure the decay to silence. `advanceUntilEnvelope`
	// counts from the envelope's current state on each call, so the decay call
	// already spans attack-complete to silence; subtracting the attack call's
	// count would short the measurement by the whole attack.
	const std::uint32_t decay_samples = advanceUntilEnvelope(envelope, [](float level) { return level < 0.001f; });
	const float measured_decay = static_cast<float>(decay_samples) / static_cast<float>(test_sample_rate_hz_envelope);

	// ASSERT
	EXPECT_NEAR(measured_decay, decay_seconds, decay_seconds * 0.2f);
}

// An envelope that has never been gated on produces no segment at all: the
// attack does not run on an ungated voice. Without this a voice held back by
// a `start_subtick` or an instance onset delay would sound at full level
// while it is supposed to be silent.
TEST(EnvelopeTests, Advance_NeverGatedOn_StaysSilent)
{
	// ARRANGE
	glirps::impl::Envelope envelope(test_sample_rate_hz_envelope);
	envelope.setRatesInSeconds(0.002f, 0.006f, 0.006f, 1.0f);

	// ACT: advance far past the attack duration without ever gating on.
	float peak = 0.0f;
	for (std::uint32_t sample = 0; sample < test_sample_rate_hz_envelope / 10; ++sample)
	{
		const float level = envelope.advance();
		if (level > peak)
			peak = level;
	}

	// ASSERT
	EXPECT_EQ(peak, 0.0f);
}

// The same envelope also reports complete, so a chip instance whose voices
// were never gated on can be reclaimed. `setGate` is edge-triggered, so the
// gate-off that a one-shot's release tick produces is a no-op on a voice whose
// gate never rose, and without this the instance would hold its pool slot
// forever.
TEST(EnvelopeTests, IsComplete_NeverGatedOn_ReportsComplete)
{
	// ARRANGE
	glirps::impl::Envelope envelope(test_sample_rate_hz_envelope);
	envelope.setRatesInSeconds(0.002f, 0.006f, 0.006f, 1.0f);

	// ACT
	const bool complete_before_gate = envelope.isComplete();
	envelope.setGate(false);
	const bool complete_after_gate_off = envelope.isComplete();

	// ASSERT
	EXPECT_TRUE(complete_before_gate);
	EXPECT_TRUE(complete_after_gate_off);
}

// A repeated gate-on does not restart the attack: re-applying a gate-on block
// to an already sounding voice must not retrigger its envelope.
TEST(EnvelopeTests, SetGate_RepeatedGateOn_DoesNotRestartTheAttack)
{
	// ARRANGE
	glirps::impl::Envelope envelope(test_sample_rate_hz_envelope);
	envelope.setRatesInSeconds(0.05f, 0.05f, 0.05f, 1.0f);
	envelope.setGate(true);
	float level_before = 0.0f;
	for (std::uint32_t sample = 0; sample < 200; ++sample)
		level_before = envelope.advance();
	ASSERT_GT(level_before, 0.0f);

	// ACT
	envelope.setGate(true);
	const float level_after = envelope.advance();

	// ASSERT: the level continued rising from where it was, rather than
	// falling back toward zero.
	EXPECT_GE(level_after, level_before);
}
