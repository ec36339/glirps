#include "impl/GritStage.hpp"

#include "glirps/GritProfile.hpp"
#include "glirps/ParameterBlock.hpp"

#include <gtest/gtest.h>

namespace
{
	// A block with distinct, non-lattice values on every quantized field, used
	// by the identity-transform and different-output tests.
	glirps::ParameterBlock arbitraryBlock()
	{
		glirps::ParameterBlock block{};
		block.cutoff_hz = 5000.37f;
		block.resonance_q = 3.14159f;
		block.output_gain = 0.6789f;
		block.voice_count = 1;
		block.voices.at(0).frequency_hz = 261.6256f;
		block.voices.at(0).pulse_width = 0.37123f;
		block.voices.at(0).gain = 0.5555f;
		return block;
	}

	// The reference cutoff bit depth over the SID-reference cutoff range
	// gives a step of ~5.8 Hz. Bucket 1000 spans
	// roughly [5877.1, 5882.9); these two sit mid-bucket, 1 Hz apart, clear
	// of both edges, so they are expected to collapse.
	constexpr float cutoff_mid_bucket_1000_a_hz = 5879.0f;
	constexpr float cutoff_mid_bucket_1000_b_hz = 5880.0f;

	// A 55 Hz base pitch and a +0.2 Hz offset, larger than the
	// SID-reference pitch step (~0.06 Hz).
	constexpr float wobble_base_frequency_hz = 55.0f;
	constexpr float wobble_offset_hz = 0.2f;

	// A 0.05 gain delta: smaller than the 4-bit `output_gain` step (~0.067),
	// larger than the 8-bit per-voice `gain` step (~0.0039).
	constexpr float gain_delta_lower = 0.60f;
	constexpr float gain_delta_higher = 0.65f;
}

// Two cutoff values inside one quantization step at the reference bit depths
// collapse to the same value.
TEST(GritStageTests, ApplyGrit_TwoCutoffsWithinOneStep_CollapseToSameValue)
{
	// ARRANGE
	glirps::GritProfile profile{};
	glirps::ParameterBlock block_a{};
	block_a.voice_count = 1;
	block_a.cutoff_hz = cutoff_mid_bucket_1000_a_hz;
	glirps::ParameterBlock block_b{};
	block_b.voice_count = 1;
	block_b.cutoff_hz = cutoff_mid_bucket_1000_b_hz;

	// ACT
	glirps::impl::applyGrit(block_a, profile, 1.0f);
	glirps::impl::applyGrit(block_b, profile, 1.0f);

	// ASSERT
	EXPECT_FLOAT_EQ(block_a.cutoff_hz, block_b.cutoff_hz);
}

// Grit scale 0 is the identity transform: every parameter keeps full float
// resolution.
TEST(GritStageTests, ApplyGrit_ScaleZero_IsIdentityTransform)
{
	// ARRANGE
	glirps::GritProfile profile{};
	glirps::ParameterBlock block = arbitraryBlock();
	const glirps::ParameterBlock original = block;

	// ACT
	glirps::impl::applyGrit(block, profile, 0.0f);

	// ASSERT
	EXPECT_FLOAT_EQ(block.cutoff_hz, original.cutoff_hz);
	EXPECT_FLOAT_EQ(block.resonance_q, original.resonance_q);
	EXPECT_FLOAT_EQ(block.output_gain, original.output_gain);
	EXPECT_FLOAT_EQ(block.voices.at(0).frequency_hz, original.voices.at(0).frequency_hz);
	EXPECT_FLOAT_EQ(block.voices.at(0).pulse_width, original.voices.at(0).pulse_width);
	EXPECT_FLOAT_EQ(block.voices.at(0).gain, original.voices.at(0).gain);
}

// The quantization step size at grit scale 0.5 lies strictly between the
// step sizes at 0 and 1 for each parameter: the scale interpolates the step
// size, so it sweeps rather than jumping at integer bit-depth boundaries.
TEST(GritStageTests, QuantizationStep_ScaleOneHalf_LiesStrictlyBetweenScaleZeroAndOne)
{
	using glirps::impl::detail::quantizationStep;

	// ARRANGE: an arbitrary bit depth and range, standing in for every
	// parameter class -- the formula is the same for each.
	constexpr std::uint32_t bits = 11;
	constexpr float range = 100.0f;

	// ACT
	const float step_at_zero = quantizationStep(bits, range, 0.0f);
	const float step_at_half = quantizationStep(bits, range, 0.5f);
	const float step_at_one = quantizationStep(bits, range, 1.0f);

	// ASSERT
	EXPECT_GT(step_at_half, step_at_zero);
	EXPECT_LT(step_at_half, step_at_one);
}

// Pitch quantizes against the SID 6581's actual accumulator-derived
// resolution (~0.06 Hz at the reference 16-bit depth), not the full Nyquist
// band, so a sub-Hz wobble survives at grit scale 1.
// Asserts the exact quantized value `applyGrit` produces, derived
// independently through the same primitives with the SID reference range.
TEST(GritStageTests, ApplyGrit_ReferenceBitDepth_PitchStepMatchesSidResolution)
{
	using glirps::impl::detail::frequency_hz_floor;
	using glirps::impl::detail::quantizationStep;
	using glirps::impl::detail::quantize;
	using glirps::impl::detail::sid_pitch_reference_range_hz;

	// ARRANGE
	glirps::GritProfile profile{};
	glirps::ParameterBlock lower{};
	lower.voice_count = 1;
	lower.voices.at(0).frequency_hz = wobble_base_frequency_hz;
	glirps::ParameterBlock higher{};
	higher.voice_count = 1;
	higher.voices.at(0).frequency_hz = wobble_base_frequency_hz + wobble_offset_hz;

	const float expected_step = quantizationStep(profile.bit_depths.pitch, sid_pitch_reference_range_hz, 1.0f);
	const float expected_lower_hz
		= quantize(wobble_base_frequency_hz, frequency_hz_floor, expected_step, profile.stepping_mode);
	const float expected_higher_hz = quantize(wobble_base_frequency_hz + wobble_offset_hz,
		frequency_hz_floor,
		expected_step,
		profile.stepping_mode);

	// ACT
	glirps::impl::applyGrit(lower, profile, 1.0f);
	glirps::impl::applyGrit(higher, profile, 1.0f);

	// ASSERT
	EXPECT_FLOAT_EQ(lower.voices.at(0).frequency_hz, expected_lower_hz);
	EXPECT_FLOAT_EQ(higher.voices.at(0).frequency_hz, expected_higher_hz);
	EXPECT_NE(expected_lower_hz, expected_higher_hz);
}

// Per-voice `gain` quantizes at the 8-bit envelope/DAC resolution, distinct
// from `output_gain`'s 4-bit master-volume resolution.
// Asserts exact quantized values for both fields, independently derived
// through the same primitives with each field's own reference bit depth.
TEST(GritStageTests, ApplyGrit_ReferenceBitDepth_VoiceGainFinerThanOutputGain)
{
	using glirps::impl::detail::gain_authored_ceiling;
	using glirps::impl::detail::gain_floor;
	using glirps::impl::detail::quantizationStep;
	using glirps::impl::detail::quantize;
	using glirps::impl::detail::sid_envelope_dac_bits;

	// ARRANGE
	glirps::GritProfile profile{};
	glirps::ParameterBlock lower{};
	lower.output_gain = gain_delta_lower;
	lower.voice_count = 1;
	lower.voices.at(0).gain = gain_delta_lower;
	glirps::ParameterBlock higher{};
	higher.output_gain = gain_delta_higher;
	higher.voice_count = 1;
	higher.voices.at(0).gain = gain_delta_higher;

	const float expected_output_gain_step
		= quantizationStep(profile.bit_depths.gain, gain_authored_ceiling - gain_floor, 1.0f);
	const float expected_voice_gain_step
		= quantizationStep(sid_envelope_dac_bits, gain_authored_ceiling - gain_floor, 1.0f);
	const float expected_lower_output_gain
		= quantize(gain_delta_lower, gain_floor, expected_output_gain_step, profile.stepping_mode);
	const float expected_higher_output_gain
		= quantize(gain_delta_higher, gain_floor, expected_output_gain_step, profile.stepping_mode);
	const float expected_lower_voice_gain
		= quantize(gain_delta_lower, gain_floor, expected_voice_gain_step, profile.stepping_mode);
	const float expected_higher_voice_gain
		= quantize(gain_delta_higher, gain_floor, expected_voice_gain_step, profile.stepping_mode);

	// ACT
	glirps::impl::applyGrit(lower, profile, 1.0f);
	glirps::impl::applyGrit(higher, profile, 1.0f);

	// ASSERT
	EXPECT_FLOAT_EQ(lower.output_gain, expected_lower_output_gain);
	EXPECT_FLOAT_EQ(higher.output_gain, expected_higher_output_gain);
	EXPECT_FLOAT_EQ(lower.output_gain, higher.output_gain);
	EXPECT_FLOAT_EQ(lower.voices.at(0).gain, expected_lower_voice_gain);
	EXPECT_FLOAT_EQ(higher.voices.at(0).gain, expected_higher_voice_gain);
	EXPECT_NE(expected_lower_voice_gain, expected_higher_voice_gain);
}

// An off-lattice cutoff quantizes to the exact value the grit stage's
// primitives give over the SID-reference cutoff range, at the reference bit
// depth and grit scale 1.
TEST(GritStageTests, ApplyGrit_OffLatticeSidCutoff_QuantizesToExpectedValue)
{
	using glirps::impl::detail::quantizationStep;
	using glirps::impl::detail::quantize;
	using glirps::impl::detail::sid_cutoff_reference_ceiling_hz;
	using glirps::impl::detail::sid_cutoff_reference_floor_hz;

	// ARRANGE
	constexpr float off_lattice_cutoff_hz = 1234.5678f;
	const glirps::GritProfile profile{};
	glirps::ParameterBlock block{};
	block.voice_count = 1;
	block.cutoff_hz = off_lattice_cutoff_hz;
	const float expected_step = quantizationStep(profile.bit_depths.cutoff,
		sid_cutoff_reference_ceiling_hz - sid_cutoff_reference_floor_hz,
		1.0f);
	const float expected_cutoff_hz
		= quantize(off_lattice_cutoff_hz, sid_cutoff_reference_floor_hz, expected_step, profile.stepping_mode);

	// ACT
	glirps::impl::applyGrit(block, profile, 1.0f);

	// ASSERT
	EXPECT_FLOAT_EQ(block.cutoff_hz, expected_cutoff_hz);
	EXPECT_NE(expected_cutoff_hz, off_lattice_cutoff_hz);
}
