#include "glirps/glirps.hpp"
#include "glirps/glirps_handles.hpp"

#include "SoundProgramTestSupport.hpp"
#include "SynthApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

// Headless integration tests for `synthUpdateControlParams` in
// `glirps/glirps.hpp`. GLIRPS is CPU-only synthesis producing a sample buffer,
// so no GPU or audio device is used.
using glirps::test_support::makeSustainedProgram;
using glirps::test_support::renderOneBaseTick;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;

// A sustained instance is held with the gate on; `synthUpdateControlParams`'s
// `gain_multiplier` of 0 silences it and 1 restores output, then the gate is
// released and the instance runs to completion.
TEST(SynthControlParamsTests, UpdateControlParams_GainMultiplier_SilencesAndRestores)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeSustainedProgram(storage);
	const glirps::CreateResult created = glirps::synthCreate(1, 4);
	ASSERT_TRUE(created);
	const glirps::SynthOwner synth_owner(created.synth);
	const glirps::SynthHandle synth = synth_owner.get();
	glirps::synthConfigure(synth, test_sample_rate_hz, test_oversampling_factor);
	const glirps::StartResult started = glirps::synthStart(synth, program, 0);
	ASSERT_TRUE(started);
	glirps::synthSetGate(synth, started.instance, true);

	// ACT: a zero multiplier silences the instance while it stays gated.
	glirps::synthUpdateControlParams(synth,
		started.instance,
		glirps::ControlParams{.flags = glirps::ControlFlag::gain_multiplier, .gain_multiplier = 0.0f});
	float silenced_peak = 0.0f;
	for (int t = 0; t < 4; ++t)
		silenced_peak = renderOneBaseTick(synth, started.instance);

	// ASSERT
	EXPECT_LT(silenced_peak, 1.0e-4f);

	// ACT: a unit multiplier produces output again.
	glirps::synthUpdateControlParams(synth,
		started.instance,
		glirps::ControlParams{.flags = glirps::ControlFlag::gain_multiplier, .gain_multiplier = 1.0f});
	float restored_peak = 0.0f;
	for (int t = 0; t < 4; ++t)
		restored_peak = std::max(restored_peak, renderOneBaseTick(synth, started.instance));

	// ASSERT
	EXPECT_GT(restored_peak, 0.0f);

	// ACT: release the gate and run to completion.
	glirps::synthSetGate(synth, started.instance, false);
	bool complete = false;
	for (int t = 0; t < 400 && !complete; ++t)
	{
		renderOneBaseTick(synth, started.instance);
		complete = glirps::synthIsComplete(synth, started.instance);
	}

	// ASSERT
	EXPECT_TRUE(complete);

	glirps::synthRelease(synth, started.instance);
}

// A flagged `cutoff_hz` of 0 overrides to the cutoff floor, observed as the
// emitted block's `cutoff_hz`; `reset_flags` restores the authored cutoff.
TEST(SynthControlParamsTests, UpdateControlParams_FlaggedZeroCutoffThenReset_FloorsThenRestoresAuthoredCutoff)
{
	// ARRANGE
	constexpr float cutoff_floor_hz = 1.0f;
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeSustainedProgram(storage);
	const glirps::CreateResult created = glirps::synthCreate(1, 4);
	ASSERT_TRUE(created);
	const glirps::SynthOwner synth_owner(created.synth);
	const glirps::SynthHandle synth = synth_owner.get();
	glirps::synthConfigure(synth, test_sample_rate_hz, test_oversampling_factor);
	glirps::synthSetGritScale(synth, 0.0f);
	const glirps::StartResult started = glirps::synthStart(synth, program, 0);
	ASSERT_TRUE(started);
	glirps::synthSetGate(synth, started.instance, true);
	const auto first_block_cutoff = [&]
	{
		std::vector<glirps::ParameterBlock> blocks(glirps::synthGetBlocksPerBaseTick(synth, started.instance));
		const std::uint32_t written = glirps::synthEvaluateTick(synth, started.instance, glirps::shims::toSpan(blocks));
		return written > 0 ? blocks.front().cutoff_hz : -1.0f;
	};
	const float authored_cutoff_hz = first_block_cutoff();

	// ACT
	glirps::synthUpdateControlParams(synth,
		started.instance,
		glirps::ControlParams{.flags = glirps::ControlFlag::cutoff_hz, .cutoff_hz = 0.0f});
	const float floored_cutoff_hz = first_block_cutoff();
	glirps::synthUpdateControlParams(synth,
		started.instance,
		glirps::ControlParams{.reset_flags = glirps::ControlFlag::cutoff_hz});
	const float restored_cutoff_hz = first_block_cutoff();

	// ASSERT
	EXPECT_FLOAT_EQ(authored_cutoff_hz, program.filter.cutoff_hz);
	EXPECT_FLOAT_EQ(floored_cutoff_hz, cutoff_floor_hz);
	EXPECT_FLOAT_EQ(restored_cutoff_hz, authored_cutoff_hz);

	glirps::synthRelease(synth, started.instance);
}
