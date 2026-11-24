#include "ChipInstanceTestAccess.hpp"
#include "glirps/constants.hpp"
#include "impl/ChipInstance.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

TEST(ChipInstanceTests, Triangle_UnityGains_PeaksAtNominalSourceLevel)
{
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	constexpr std::uint32_t frame_count = 48000;
	glirps::impl::ChipInstance chip(sample_rate_hz, 1, 0x5a5au);
	chip.setVoice(0, glirps::Waveform::triangle, 220.0f, 0.5f, 1.0f, 0x5a5au);
	chip.setOutputGain(1.0f);

	// ACT
	std::vector<float> samples(frame_count);
	for (float& sample : samples)
		sample = chip.advance();
	const float peak = *std::max_element(samples.begin(),
		samples.end(),
		[](float a, float b) { return std::fabs(a) < std::fabs(b); });

	// ASSERT
	const float peak_dbfs = glirps::decibel_amplitude_scale * std::log10(std::fabs(peak) / glirps::full_scale);
	EXPECT_NEAR(peak_dbfs, glirps::nominal_source_level_db, 0.5);
}

// A cutoff step from `applyFilterBlock` reaches its target only after
// `sub_block_frame_count` samples, not on the first one: the cutoff is
// ramped linearly across the sub-block to avoid zipper noise.
TEST(ChipInstanceTests, ApplyFilterBlock_CutoffStep_RampsOverOneSubBlock)
{
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	glirps::impl::ChipInstance chip(sample_rate_hz, 1, 0x5a5au);
	constexpr glirps::FilterModeSet lowpass_only{.lowpass = true, .bandpass = false, .highpass = false, .notch = false};
	chip.resetFilterBlock(1000.0f, 0.707f, lowpass_only);

	// ACT
	chip.applyFilterBlock(9000.0f, 0.707f, lowpass_only);
	chip.advance();
	const float cutoff_after_one_sample = glirps::impl::ChipInstanceTestAccess::cutoffHz(chip);
	for (std::uint32_t i = 1; i < glirps::sub_block_frame_count; ++i)
		chip.advance();
	const float cutoff_after_sub_block = glirps::impl::ChipInstanceTestAccess::cutoffHz(chip);

	// ASSERT
	EXPECT_LT(cutoff_after_one_sample, 9000.0f);
	EXPECT_GT(cutoff_after_one_sample, 1000.0f);
	EXPECT_NEAR(cutoff_after_sub_block, 9000.0f, 0.01f);
}

// A pitch step from `setVoiceParameters` reaches its target on the first
// sample: pitch steps are not smoothed, so the oscillator's phase
// increment is replaced immediately rather than ramped like gain.
// Measured as the period of the waveform right after the step, which
// would be stretched toward the old period if the step were smoothed.
TEST(ChipInstanceTests, SetVoiceParameters_PitchStep_ReachesTargetOnFirstSample)
{
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	glirps::impl::ChipInstance chip(sample_rate_hz, 1, 0x5a5au);
	chip.setVoice(0, glirps::Waveform::triangle, 440.0f, 0.5f, 1.0f, 0x5a5au);
	// Let the 440 Hz tone establish a steady phase.
	for (std::uint32_t i = 0; i < 4800; ++i)
		chip.advance();

	// ACT: step the pitch ten times higher, then render past the step.
	chip.setVoiceParameters(0, glirps::Waveform::triangle, 4400.0f, 0.5f, 1.0f);
	std::vector<float> after(2000);
	for (float& sample : after)
		sample = chip.advance();

	// ASSERT: find the first two rising zero crossings after the step,
	// confirming both were found. Two rising crossings of a triangle are one
	// full period apart, 48000 / 4400 ~= 10.9 samples at the stepped
	// frequency. A ramped step would pull the first intervals toward the old
	// 440 Hz period of 109 samples.
	std::optional<std::uint32_t> first_rising;
	std::optional<std::uint32_t> second_rising;
	float previous = after.front();
	std::uint32_t i = 0;
	for (const float sample : after)
	{
		if (i > 0 && previous < 0.0f && sample >= 0.0f)
		{
			if (!first_rising)
				first_rising = i;
			else
			{
				second_rising = i;
				break;
			}
		}
		previous = sample;
		++i;
	}
	ASSERT_TRUE(first_rising.has_value());
	ASSERT_TRUE(second_rising.has_value());

	const float spacing = static_cast<float>(*second_rising - *first_rising);
	EXPECT_NEAR(spacing, 48000.0f / 4400.0f, 2.0f);
}

// A resonance step and a per-voice gain step each reach their target only
// after `sub_block_frame_count` samples: both are ramped linearly across the
// sub-block to avoid zipper noise.
TEST(ChipInstanceTests, ApplyBlock_ResonanceAndGainSteps_RampOverOneSubBlock)
{
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	glirps::impl::ChipInstance chip(sample_rate_hz, 1, 0x5a5au);
	constexpr glirps::FilterModeSet lowpass_only{.lowpass = true, .bandpass = false, .highpass = false, .notch = false};
	chip.resetFilterBlock(1000.0f, 1.0f, lowpass_only);
	chip.setVoice(0, glirps::Waveform::triangle, 220.0f, 0.5f, 0.2f, 0x5a5au);

	// ACT
	chip.applyFilterBlock(1000.0f, 10.0f, lowpass_only);
	chip.setVoiceParameters(0, glirps::Waveform::triangle, 220.0f, 0.5f, 0.9f);
	chip.advance();
	const float resonance_after_one_sample = glirps::impl::ChipInstanceTestAccess::resonanceQ(chip);
	const float gain_after_one_sample = glirps::impl::ChipInstanceTestAccess::voiceGain(chip, 0);
	for (std::uint32_t i = 1; i < glirps::sub_block_frame_count; ++i)
		chip.advance();
	const float resonance_after_sub_block = glirps::impl::ChipInstanceTestAccess::resonanceQ(chip);
	const float gain_after_sub_block = glirps::impl::ChipInstanceTestAccess::voiceGain(chip, 0);

	// ASSERT
	EXPECT_LT(resonance_after_one_sample, 10.0f);
	EXPECT_GT(resonance_after_one_sample, 1.0f);
	EXPECT_NEAR(resonance_after_sub_block, 10.0f, 0.001f);

	EXPECT_LT(gain_after_one_sample, 0.9f);
	EXPECT_GT(gain_after_one_sample, 0.2f);
	EXPECT_NEAR(gain_after_sub_block, 0.9f, 0.0001f);
}

// A ramp clamps at its target once reached: a direct `BlockRenderer` caller
// (an offline caller driving the renderer without a `Synth`) rendering more
// than one sub-block after a single `applyBlock` must not overshoot the target
// and keep climbing, per the `applyBlock` contract: a step ramps over one
// sub-block and then holds.
TEST(ChipInstanceTests, Ramps_ClampAtTarget_DoNotOvershootPastOneSubBlock)
{
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	glirps::impl::ChipInstance chip(sample_rate_hz, 1, 0x5a5au);
	constexpr glirps::FilterModeSet lowpass_only{.lowpass = true, .bandpass = false, .highpass = false, .notch = false};
	chip.resetFilterBlock(1000.0f, 0.707f, lowpass_only);
	chip.setVoice(0, glirps::Waveform::triangle, 220.0f, 0.5f, 0.9f, 0x5a5au);

	// ACT: one `applyFilterBlock` upward and one voice-gain step downward,
	// then render two sub-blocks with no further block applied.
	chip.applyFilterBlock(9000.0f, 0.707f, lowpass_only);
	chip.setVoiceParameters(0, glirps::Waveform::triangle, 220.0f, 0.5f, 0.2f);
	for (std::uint32_t i = 0; i < 2 * glirps::sub_block_frame_count; ++i)
		chip.advance();
	const float cutoff_after_two_sub_blocks = glirps::impl::ChipInstanceTestAccess::cutoffHz(chip);
	const float gain_after_two_sub_blocks = glirps::impl::ChipInstanceTestAccess::voiceGain(chip, 0);

	// ASSERT: each ramp ended exactly at its target, not past it.
	EXPECT_NEAR(cutoff_after_two_sub_blocks, 9000.0f, 0.01f);
	EXPECT_LE(cutoff_after_two_sub_blocks, 9000.0f);
	EXPECT_NEAR(gain_after_two_sub_blocks, 0.2f, 0.0001f);
	EXPECT_GE(gain_after_two_sub_blocks, 0.2f);
}
